#include "applemusic.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {

// Apple returns the thumbnail at whatever size the caller asks for by path, so
// the 100 px URL is rewritten rather than fetched twice.
QString artworkAt600(const QString &artworkUrl100)
{
    return QString(artworkUrl100).replace(QStringLiteral("100x100bb"), QStringLiteral("600x600bb"));
}

QString formatDuration(qint64 milliseconds)
{
    if (milliseconds <= 0)
        return {};
    const qint64 totalSeconds = milliseconds / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

AppleMusic::AppleMusic(QObject *parent) : QObject(parent) {}

AppleMusic::~AppleMusic()
{
    if (m_reply)
        m_reply->abort();
}

void AppleMusic::setRegion(const QString &region)
{
    const QString upper = region.toUpper();
    if (upper.isEmpty() || upper == m_region)
        return;
    m_region = upper;
    emit regionChanged();
}

void AppleMusic::fail(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    emit failed(message);
}

QVariantList AppleMusic::parseResults(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonArray results = root.value(QStringLiteral("results")).toArray();

    QVariantList tracks;
    for (const QJsonValue &value : results) {
        const QJsonObject song = value.toObject();
        const QString title = song.value(QStringLiteral("trackName")).toString();
        if (title.isEmpty())
            continue;  // collections and artists share this endpoint; skip them

        const QString preview = song.value(QStringLiteral("previewUrl")).toString();
        const QString webUrl = song.value(QStringLiteral("trackViewUrl")).toString();
        const QString id = QString::number(song.value(QStringLiteral("trackId")).toVariant().toLongLong());

        QVariantMap track;
        track.insert(QStringLiteral("id"), id);
        track.insert(QStringLiteral("title"), title);
        track.insert(QStringLiteral("artist"), song.value(QStringLiteral("artistName")).toString());
        track.insert(QStringLiteral("album"), song.value(QStringLiteral("collectionName")).toString());
        track.insert(QStringLiteral("duration"),
                     formatDuration(song.value(QStringLiteral("trackTimeMillis")).toVariant().toLongLong()));
        track.insert(QStringLiteral("thumbnail"),
                     artworkAt600(song.value(QStringLiteral("artworkUrl100")).toString()));
        // A preview is what makes an Apple row playable at all, so `url` is the
        // preview and `playable` says whether there is one.
        track.insert(QStringLiteral("url"), preview);
        track.insert(QStringLiteral("playable"), !preview.isEmpty());
        track.insert(QStringLiteral("webUrl"), webUrl);
        track.insert(QStringLiteral("source"), QStringLiteral("apple"));
        tracks.append(track);
    }
    return tracks;
}

void AppleMusic::search(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;
    if (m_reply) {
        fail(tr("still working on: %1").arg(m_pendingQuery));
        return;
    }
    m_pendingQuery = trimmed;
    m_lastError.clear();
    emit lastErrorChanged();

    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("term"), trimmed);
    params.addQueryItem(QStringLiteral("media"), QStringLiteral("music"));
    params.addQueryItem(QStringLiteral("entity"), QStringLiteral("song"));
    params.addQueryItem(QStringLiteral("limit"), QStringLiteral("30"));
    params.addQueryItem(QStringLiteral("country"), m_region);
    url.setQuery(params);

    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("huruwave/0.1 (+Qt)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_network.get(request);
    emit busyChanged();
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        emit busyChanged();

        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("Apple Music search failed: %1").arg(reply->errorString()));
            return;
        }
        const QVariantList tracks = parseResults(reply->readAll());
        if (tracks.isEmpty())
            fail(tr("Apple Music returned nothing for \"%1\"").arg(m_pendingQuery));
        else
            emit tracksReady(tracks);
    });
}
