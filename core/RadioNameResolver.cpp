#include "RadioNameResolver.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace {
constexpr int kMaxParallel = 2;
constexpr int kProbeTimeoutMs = 8000;
}

RadioProbeWorker::RadioProbeWorker(QObject *parent) : QObject(parent) {}

QNetworkAccessManager *RadioProbeWorker::manager()
{
    if (!m_manager)
        m_manager = new QNetworkAccessManager(this);
    return m_manager;
}

void RadioProbeWorker::resolve(const QString &url)
{
    if (m_tried.contains(url) || m_active.contains(url) || m_queue.contains(url))
        return;

    m_queue.enqueue(url);
    startNext();
}

void RadioProbeWorker::startNext()
{
    while (m_running < kMaxParallel && !m_queue.isEmpty()) {
        const QString url = m_queue.dequeue();
        m_active.insert(url);
        ++m_running;

        QNetworkRequest request{QUrl(url)};
        // Ask for the ICY headers; without this some servers do not send
        // icy-name at all.
        request.setRawHeader("Icy-MetaData", "1");
        request.setRawHeader("User-Agent", "Luma");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply *reply = manager()->get(request);
        auto done = std::make_shared<bool>(false);
        auto finishProbe = [this, reply, url, done](const QString &name) {
            if (*done) return;
            *done = true;
            finish(url, reply, name);
        };

        // Wait for the name but do not abort on the first headers: they may
        // belong to a redirect. The body is never read, though: as soon as
        // data arrives without a name, the probe is over.
        connect(reply, &QNetworkReply::metaDataChanged, this, [reply, finishProbe]() {
            const QString name = decodeIcyName(reply->rawHeader("icy-name"));
            if (!name.isEmpty()) finishProbe(name);
        });
        connect(reply, &QNetworkReply::readyRead, this, [reply, finishProbe]() {
            finishProbe(decodeIcyName(reply->rawHeader("icy-name")));
        });
        connect(reply, &QNetworkReply::finished, this, [reply, finishProbe]() {
            finishProbe(decodeIcyName(reply->rawHeader("icy-name")));
        });
        QTimer::singleShot(kProbeTimeoutMs, reply, [finishProbe]() {
            finishProbe(QString());
        });
    }
}

void RadioProbeWorker::finish(const QString &url, QNetworkReply *reply, const QString &name)
{
    m_active.remove(url);
    m_tried.insert(url);
    if (m_running > 0) --m_running;

    if (!name.isEmpty() && name != url)
        emit nameResolved(url, name);

    reply->abort();
    reply->deleteLater();
    startNext();
}

QString RadioProbeWorker::decodeIcyName(const QByteArray &raw)
{
    if (raw.isEmpty()) return QString();

    QString text = QString::fromUtf8(raw);
    // Older servers put a legacy 8-bit encoding in the header; UTF-8 decoding
    // of such bytes produces replacement characters.
    if (text.contains(QChar::ReplacementCharacter))
        text = QString::fromLatin1(raw);
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
        text = text.mid(1, text.size() - 2).trimmed();
    return text;
}

RadioNameResolver::RadioNameResolver(QObject *parent) : QObject(parent)
{
    m_worker = new RadioProbeWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &RadioProbeWorker::nameResolved, this, &RadioNameResolver::nameResolved);
    m_thread.setObjectName(QStringLiteral("RadioNameResolver"));
    m_thread.start();
}

RadioNameResolver::~RadioNameResolver()
{
    m_thread.quit();
    m_thread.wait();
}

void RadioNameResolver::resolve(const QString &url)
{
    // The test suites replace yt-dlp and other helpers with fakes; they must
    // not make real network requests.
    if (qEnvironmentVariableIsSet("LUMA_TEST_NO_NETWORK"))
        return;

    const QUrl parsed(url);
    if (!parsed.isValid()
            || (parsed.scheme() != QLatin1String("http")
                && parsed.scheme() != QLatin1String("https")))
        return;

    // The validation above is cheap; the actual probe (and the first network
    // initialisation) happens in the worker thread.
    QMetaObject::invokeMethod(m_worker, "resolve", Qt::QueuedConnection,
                              Q_ARG(QString, url));
}
