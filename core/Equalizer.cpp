#include "Equalizer.hpp"

#include "ConfigManager.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

constexpr double kMinGain = -12.0;
constexpr double kMaxGain = 12.0;

const QVector<double> &frequenciesFor(int bands)
{
    // Classic octave bands and a half-octave layout that contains the same
    // centres, so switching layouts round-trips the curve.
    static const QVector<double> ten = {
        31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
    };
    static const QVector<double> twenty = {
        20, 31.5, 44, 63, 88, 125, 177, 250, 354, 500,
        707, 1000, 1414, 2000, 2828, 4000, 5657, 8000, 11314, 16000
    };
    return bands == 20 ? twenty : ten;
}

struct BuiltinPreset {
    const char *id;
    std::array<double, 10> gains;
};

// Common graphic-EQ curves for the classic 10-band layout.
const BuiltinPreset kBuiltinPresets[] = {
    {"flat",       {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"rock",       {5, 4, 2, 0, -1, -1, 1, 3, 4, 5}},
    {"pop",        {-1, 2, 4, 5, 3, 0, -1, -1, 0, 1}},
    {"jazz",       {4, 3, 1, 2, -1, -1, 0, 1, 2, 3}},
    {"classical",  {4, 3, 2, 1, 0, -1, -1, 0, 2, 3}},
    {"electronic", {5, 4, 1, 0, -2, 0, 1, 3, 4, 5}},
    {"hiphop",     {6, 5, 3, 0, -1, -1, 1, 2, 3, 4}},
    {"vocal",      {-2, -1, 0, 2, 4, 4, 3, 1, 0, -1}},
    {"bass",       {7, 6, 4, 2, 0, 0, 0, 0, 0, 0}},
    {"treble",     {0, 0, 0, 0, 0, 0, 2, 4, 6, 7}},
};

const BuiltinPreset *findBuiltin(const QString &id)
{
    for (const BuiltinPreset &preset : kBuiltinPresets) {
        if (id == QLatin1String(preset.id))
            return &preset;
    }
    return nullptr;
}

QString formatNumber(double value)
{
    if (std::abs(value - std::round(value)) < 0.001)
        return QString::number(static_cast<int>(std::round(value)));
    return QString::number(value, 'f', 1);
}

QString gainsToString(const QVector<double> &gains)
{
    QStringList parts;
    parts.reserve(gains.size());
    for (double gain : gains)
        parts << QString::number(gain, 'f', 1);
    return parts.join(QLatin1Char(','));
}

QVector<double> gainsFromString(const QString &text, int count)
{
    QVector<double> gains(count, 0.0);
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (int i = 0; i < count && i < parts.size(); ++i) {
        bool ok = false;
        const double gain = parts.at(i).trimmed().toDouble(&ok);
        if (ok)
            gains[i] = qBound(kMinGain, gain, kMaxGain);
    }
    return gains;
}

// Linear interpolation in the log-frequency domain: builtin presets are
// defined for 10 bands, but must also work in the 20-band layout.
QVector<double> interpolateGains(const QVector<double> &fromFrequencies,
                                 const QVector<double> &fromGains,
                                 const QVector<double> &toFrequencies)
{
    QVector<double> result(toFrequencies.size(), 0.0);
    if (fromFrequencies.isEmpty() || fromFrequencies.size() != fromGains.size())
        return result;

    for (int i = 0; i < toFrequencies.size(); ++i) {
        const double frequency = toFrequencies.at(i);
        if (frequency <= fromFrequencies.first()) {
            result[i] = fromGains.first();
            continue;
        }
        if (frequency >= fromFrequencies.last()) {
            result[i] = fromGains.last();
            continue;
        }
        for (int j = 1; j < fromFrequencies.size(); ++j) {
            if (frequency > fromFrequencies.at(j))
                continue;
            const double low = std::log(fromFrequencies.at(j - 1));
            const double high = std::log(fromFrequencies.at(j));
            const double position = (std::log(frequency) - low) / (high - low);
            result[i] = fromGains.at(j - 1)
                        + position * (fromGains.at(j) - fromGains.at(j - 1));
            break;
        }
    }
    return result;
}

} // namespace

Equalizer::Equalizer(ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_gains10(10, 0.0)
    , m_gains20(20, 0.0)
{
    m_applyTimer = new QTimer(this);
    m_applyTimer->setSingleShot(true);
    m_applyTimer->setInterval(60);
    connect(m_applyTimer, &QTimer::timeout, this, &Equalizer::apply);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(300);
    connect(m_saveTimer, &QTimer::timeout, this, &Equalizer::save);

    load();

    // luma.conf is watched by ConfigManager; reload the curve when another
    // instance changes it, so all windows share the equalizer.
    if (m_config)
        connect(m_config, &ConfigManager::settingsReloaded,
                this, &Equalizer::reloadFromDisk);
}

void Equalizer::reloadFromDisk()
{
    if (!m_config) return;

    const bool oldEnabled = m_enabled;
    const int oldBands = m_bands;
    const double oldPreamp = m_preamp;
    const QVector<double> oldGains10 = m_gains10;
    const QVector<double> oldGains20 = m_gains20;
    const QString oldPresetId = m_presetId;
    const QVector<CustomPreset> oldCustomPresets = m_customPresets;

    load();

    const bool curvesChanged = oldGains10 != m_gains10 || oldGains20 != m_gains20;
    const bool presetsChanged = oldCustomPresets.size() != m_customPresets.size()
        || [&]() {
               for (int i = 0; i < oldCustomPresets.size(); ++i) {
                   const CustomPreset &a = oldCustomPresets.at(i);
                   const CustomPreset &b = m_customPresets.at(i);
                   if (a.name != b.name || a.bands != b.bands
                           || a.preamp != b.preamp || a.gains != b.gains)
                       return true;
               }
               return false;
           }();

    if (oldBands != m_bands) emit bandsChanged();
    if (oldPreamp != m_preamp) emit preampChanged();
    if (curvesChanged) emit gainsChanged();
    if (oldEnabled != m_enabled) emit enabledChanged();
    if (oldPresetId != m_presetId) emit presetChanged();
    if (presetsChanged) emit customPresetsChanged();

    if (oldEnabled != m_enabled || oldBands != m_bands
            || oldPreamp != m_preamp || curvesChanged) {
        scheduleApply();
    }
}

Equalizer::~Equalizer()
{
    // Flush a pending debounced write on shutdown.
    if (m_saveTimer && m_saveTimer->isActive()) {
        m_saveTimer->stop();
        save();
    }
}

void Equalizer::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
    scheduleSave();
    scheduleApply();
}

void Equalizer::setBands(int bands)
{
    if (bands != 10 && bands != 20)
        return;
    if (m_bands == bands)
        return;

    const QVector<double> converted = interpolateGains(frequenciesFor(m_bands),
                                                       activeGains(),
                                                       frequenciesFor(bands));
    m_bands = bands;
    gainsFor(bands) = converted;

    // A builtin preset stays valid after resampling; a custom one was saved
    // for one specific layout.
    if (m_presetId.startsWith(QLatin1String("custom:")))
        setPresetId(QString());

    emit bandsChanged();
    emit gainsChanged();
    scheduleSave();
    scheduleApply();
}

void Equalizer::setPreamp(double preamp)
{
    const double value = qBound(kMinGain, std::round(preamp * 2.0) / 2.0, kMaxGain);
    if (std::abs(m_preamp - value) < 0.001)
        return;
    m_preamp = value;
    setPresetId(QString());
    emit preampChanged();
    scheduleSave();
    scheduleApply();
}

void Equalizer::setGain(int index, double gain)
{
    QVector<double> &gains = activeGains();
    if (index < 0 || index >= gains.size())
        return;
    const double value = qBound(kMinGain, std::round(gain * 2.0) / 2.0, kMaxGain);
    if (std::abs(gains.at(index) - value) < 0.001)
        return;
    gains[index] = value;
    setPresetId(QString());
    emit gainsChanged();
    scheduleSave();
    scheduleApply();
}

void Equalizer::applyBuiltinPreset(const QString &id)
{
    const BuiltinPreset *preset = findBuiltin(id);
    if (!preset)
        return;

    QVector<double> ten(10);
    for (int i = 0; i < ten.size(); ++i)
        ten[i] = preset->gains.at(i);

    if (m_bands == 10)
        m_gains10 = ten;
    else
        m_gains20 = interpolateGains(frequenciesFor(10), ten, frequenciesFor(20));

    // Builtin curves are full presets: they reset the preamp too, otherwise the
    // preset name would not describe the audible result.
    if (std::abs(m_preamp) > 0.001) {
        m_preamp = 0.0;
        emit preampChanged();
    }

    setPresetId(QStringLiteral("builtin:") + id);
    emit gainsChanged();
    scheduleSave();
    scheduleApply();
}

void Equalizer::applyCustomPreset(const QString &name)
{
    const CustomPreset *preset = nullptr;
    for (const CustomPreset &candidate : std::as_const(m_customPresets)) {
        if (candidate.name == name) {
            preset = &candidate;
            break;
        }
    }
    if (!preset)
        return;

    if (m_bands != preset->bands) {
        m_bands = preset->bands;
        emit bandsChanged();
    }
    QVector<double> gains = preset->gains;
    gains.resize(frequenciesFor(m_bands).size());
    gainsFor(m_bands) = gains;

    if (std::abs(m_preamp - preset->preamp) > 0.001) {
        m_preamp = preset->preamp;
        emit preampChanged();
    }

    setPresetId(QStringLiteral("custom:") + preset->name);
    emit gainsChanged();
    scheduleSave();
    scheduleApply();
}

void Equalizer::saveCustomPreset(const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return;

    CustomPreset preset;
    preset.name = name;
    preset.bands = m_bands;
    preset.preamp = m_preamp;
    preset.gains = activeGains();

    bool replaced = false;
    for (CustomPreset &existing : m_customPresets) {
        if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
            existing = preset;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        m_customPresets.append(preset);

    setPresetId(QStringLiteral("custom:") + name);
    emit customPresetsChanged();
    scheduleSave();
}

void Equalizer::resetCustomPresets()
{
    if (m_customPresets.isEmpty())
        return;
    m_customPresets.clear();
    if (m_presetId.startsWith(QLatin1String("custom:")))
        setPresetId(QString());
    emit customPresetsChanged();
    scheduleSave();
}

QVariantList Equalizer::frequencies() const
{
    QVariantList list;
    for (double frequency : frequenciesFor(m_bands))
        list << frequency;
    return list;
}

QVariantList Equalizer::gains() const
{
    QVariantList list;
    for (double gain : activeGains())
        list << gain;
    return list;
}

QStringList Equalizer::builtinPresets() const
{
    QStringList ids;
    for (const BuiltinPreset &preset : kBuiltinPresets)
        ids << QString::fromLatin1(preset.id);
    return ids;
}

QVariantList Equalizer::customPresets() const
{
    QVariantList list;
    for (const CustomPreset &preset : m_customPresets) {
        QVariantList gains;
        for (double gain : preset.gains)
            gains << gain;
        QVariantMap entry;
        entry[QStringLiteral("name")] = preset.name;
        entry[QStringLiteral("bands")] = preset.bands;
        entry[QStringLiteral("preamp")] = preset.preamp;
        entry[QStringLiteral("gains")] = gains;
        list << entry;
    }
    return list;
}

QString Equalizer::ffmpegFilter() const
{
    if (!m_enabled)
        return QString();

    QStringList filters;
    if (std::abs(m_preamp) > 0.01) {
        // The FFmpeg volume filter takes a linear factor; preamp is stored in dB.
        const double factor = std::pow(10.0, m_preamp / 20.0);
        filters << QStringLiteral("volume=volume=%1").arg(QString::number(factor, 'f', 4));
    }

    const QVector<double> &frequencies = frequenciesFor(m_bands);
    const QVector<double> &gains = activeGains();
    const QString width = m_bands == 20 ? QStringLiteral("0.5") : QStringLiteral("1");
    for (int i = 0; i < gains.size() && i < frequencies.size(); ++i) {
        if (std::abs(gains.at(i)) < 0.01)
            continue;
        filters << QStringLiteral("equalizer=f=%1:width_type=o:width=%2:g=%3")
                       .arg(formatNumber(frequencies.at(i)), width, formatNumber(gains.at(i)));
    }

    return filters.join(QLatin1Char(','));
}

QString Equalizer::audioFilter() const
{
    const QString graph = ffmpegFilter();
    if (graph.isEmpty())
        return QString();
    return QStringLiteral("lavfi=[") + graph + QStringLiteral("]");
}

QVector<double> &Equalizer::activeGains()
{
    return m_bands == 20 ? m_gains20 : m_gains10;
}

const QVector<double> &Equalizer::activeGains() const
{
    return m_bands == 20 ? m_gains20 : m_gains10;
}

QVector<double> &Equalizer::gainsFor(int bands)
{
    return bands == 20 ? m_gains20 : m_gains10;
}

void Equalizer::setPresetId(const QString &id)
{
    if (m_presetId == id)
        return;
    m_presetId = id;
    emit presetChanged();
}

void Equalizer::scheduleApply()
{
    if (!m_applyTimer)
        return;
    m_applyTimer->start();
}

void Equalizer::apply()
{
    const QString filter = audioFilter();
    if (filter.isEmpty() && !m_filterApplied)
        return;
    m_filterApplied = !filter.isEmpty();
    emit filterRequested(filter);
}

void Equalizer::applyNow()
{
    apply();
}

void Equalizer::scheduleSave()
{
    if (!m_saveTimer)
        return;
    m_saveTimer->start();
}

void Equalizer::save()
{
    if (!m_config)
        return;

    m_config->setValue(QStringLiteral("equalizer/enabled"), m_enabled);
    m_config->setValue(QStringLiteral("equalizer/bands"), m_bands);
    m_config->setValue(QStringLiteral("equalizer/preamp"), m_preamp);
    m_config->setValue(QStringLiteral("equalizer/gains10"), gainsToString(m_gains10));
    m_config->setValue(QStringLiteral("equalizer/gains20"), gainsToString(m_gains20));
    m_config->setValue(QStringLiteral("equalizer/preset"), m_presetId);

    QJsonArray presets;
    for (const CustomPreset &preset : m_customPresets) {
        QJsonArray gains;
        for (double gain : preset.gains)
            gains.append(gain);
        QJsonObject object;
        object.insert(QStringLiteral("name"), preset.name);
        object.insert(QStringLiteral("bands"), preset.bands);
        object.insert(QStringLiteral("preamp"), preset.preamp);
        object.insert(QStringLiteral("gains"), gains);
        presets.append(object);
    }
    m_config->setValue(QStringLiteral("equalizer/custom_presets"),
                       QString::fromUtf8(QJsonDocument(presets).toJson(QJsonDocument::Compact)));
}

void Equalizer::load()
{
    if (!m_config)
        return;

    m_enabled = m_config->value(QStringLiteral("equalizer/enabled"), false).toBool();
    m_bands = m_config->value(QStringLiteral("equalizer/bands"), 10).toInt() == 20 ? 20 : 10;
    m_preamp = qBound(kMinGain,
                      m_config->value(QStringLiteral("equalizer/preamp"), 0.0).toDouble(),
                      kMaxGain);
    m_gains10 = gainsFromString(m_config->value(QStringLiteral("equalizer/gains10")).toString(), 10);
    m_gains20 = gainsFromString(m_config->value(QStringLiteral("equalizer/gains20")).toString(), 20);

    m_customPresets.clear();
    const QJsonDocument document = QJsonDocument::fromJson(
        m_config->value(QStringLiteral("equalizer/custom_presets")).toString().toUtf8());
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        CustomPreset preset;
        preset.name = object.value(QStringLiteral("name")).toString().trimmed();
        if (preset.name.isEmpty())
            continue;
        preset.bands = object.value(QStringLiteral("bands")).toInt(10) == 20 ? 20 : 10;
        preset.preamp = qBound(kMinGain,
                               object.value(QStringLiteral("preamp")).toDouble(0.0),
                               kMaxGain);
        const QJsonArray gains = object.value(QStringLiteral("gains")).toArray();
        preset.gains.resize(frequenciesFor(preset.bands).size());
        for (int i = 0; i < preset.gains.size() && i < gains.size(); ++i)
            preset.gains[i] = qBound(kMinGain, gains.at(i).toDouble(0.0), kMaxGain);
        m_customPresets.append(preset);
    }

    m_presetId = m_config->value(QStringLiteral("equalizer/preset"), QStringLiteral("builtin:flat")).toString();
    if (m_presetId.startsWith(QLatin1String("builtin:"))) {
        if (!findBuiltin(m_presetId.mid(8)))
            m_presetId.clear();
    } else if (m_presetId.startsWith(QLatin1String("custom:"))) {
        const QString name = m_presetId.mid(7);
        const bool found = std::any_of(m_customPresets.cbegin(), m_customPresets.cend(),
                                       [&name](const CustomPreset &preset) {
                                           return preset.name == name;
                                       });
        if (!found)
            m_presetId.clear();
    }
}
