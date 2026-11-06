#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class ConfigManager;
class QTimer;

// Graphic equalizer for local playback. The band gains are turned into an
// FFmpeg filter graph and applied to mpv's "af" property; the settings and the
// user's own presets live in luma.conf.
class Equalizer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int bands READ bands WRITE setBands NOTIFY bandsChanged)
    Q_PROPERTY(double preamp READ preamp WRITE setPreamp NOTIFY preampChanged)
    // Band centres and gains of the active 10- or 20-band layout.
    Q_PROPERTY(QVariantList frequencies READ frequencies NOTIFY bandsChanged)
    Q_PROPERTY(QVariantList gains READ gains NOTIFY gainsChanged)
    Q_PROPERTY(QStringList builtinPresets READ builtinPresets CONSTANT)
    Q_PROPERTY(QVariantList customPresets READ customPresets NOTIFY customPresetsChanged)
    // "" when the curve was changed by hand, "builtin:<id>" or "custom:<name>".
    Q_PROPERTY(QString presetId READ presetId NOTIFY presetChanged)

public:
    explicit Equalizer(ConfigManager *config, QObject *parent = nullptr);
    ~Equalizer() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    int bands() const { return m_bands; }
    void setBands(int bands);

    double preamp() const { return m_preamp; }
    void setPreamp(double preamp);

    QVariantList frequencies() const;
    QVariantList gains() const;
    QStringList builtinPresets() const;
    QVariantList customPresets() const;
    QString presetId() const { return m_presetId; }

    Q_INVOKABLE void setGain(int index, double gain);
    Q_INVOKABLE void applyBuiltinPreset(const QString &id);
    Q_INVOKABLE void applyCustomPreset(const QString &name);
    Q_INVOKABLE void saveCustomPreset(const QString &name);
    Q_INVOKABLE void resetCustomPresets();

    // FFmpeg filter graph for this curve (empty when off or flat). Used by the
    // Chromecast HLS encoder and, wrapped in "lavfi=[]", by mpv.
    QString ffmpegFilter() const;
    // Value for mpv's "af" property; empty when the equalizer is off or flat.
    QString audioFilter() const;

    // Re-emits filterRequested() for the current state (used once mpv exists).
    Q_INVOKABLE void applyNow();

signals:
    void enabledChanged();
    void bandsChanged();
    void preampChanged();
    void gainsChanged();
    void customPresetsChanged();
    void presetChanged();
    // The filter graph to put into mpv's "af" property.
    void filterRequested(const QString &filter);

private:
    struct CustomPreset {
        QString name;
        int bands = 10;
        double preamp = 0.0;
        QVector<double> gains;
    };

    QVector<double> &activeGains();
    const QVector<double> &activeGains() const;
    QVector<double> &gainsFor(int bands);
    void setPresetId(const QString &id);
    void scheduleApply();
    void apply();
    void scheduleSave();
    void save();
    void load();

    ConfigManager *m_config = nullptr;
    QTimer *m_applyTimer = nullptr;
    // Slider drags change the curve many times per second; persist once the
    // user stops moving instead of rewriting luma.conf on every step.
    QTimer *m_saveTimer = nullptr;

    bool m_enabled = false;
    int m_bands = 10;
    double m_preamp = 0.0;
    QVector<double> m_gains10;
    QVector<double> m_gains20;
    QVector<CustomPreset> m_customPresets;
    QString m_presetId;
    // Whether a non-empty filter is currently installed in mpv. An empty
    // filter is only written when there is something to remove, so the
    // equalizer does not wipe an "af" chain from the user's mpv.conf on start.
    bool m_filterApplied = false;
};
