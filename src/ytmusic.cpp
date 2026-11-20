#include "ytmusic.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {

const QString kCookiesFile = QStringLiteral("/.cache/huruwave/cookies.txt");
const QString kMusicHome = QStringLiteral("https://music.youtube.com/");
const QString kClientName = QStringLiteral("WEB_REMIX");

// music.youtube.com answers a client with no consent marker by redirecting to
// consent.youtube.com, whose page carries no API key -- measured: HTTP 200,
// 583 KB, final URL .../consent.youtube.com/m?continue=...&gl=GB. This is the
// marker a browser would have stored after accepting the interstitial; it is a
// consent flag, not a credential.
const QByteArray kConsentCookie = QByteArrayLiteral("SOCS=CAI");

// Everything that talks to YouTube needs the browser-shaped agent, or the reply
// is a bot wall rather than data.
const QString kBrowserAgent = QStringLiteral(
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36");

// The API answers with neutralino-style renderers; the id lives here.
QString videoIdOf(const QJsonObject &renderer)
{
    static const QStringList paths[] = {
        {QStringLiteral("playlistItemData"), QStringLiteral("videoId")},
        {QStringLiteral("navigationEndpoint"), QStringLiteral("watchEndpoint"), QStringLiteral("videoId")},
        {QStringLiteral("overlay"), QStringLiteral("musicItemThumbnailOverlayRenderer"),
         QStringLiteral("content"), QStringLiteral("musicPlayButtonRenderer"),
         QStringLiteral("playNavigationEndpoint"), QStringLiteral("watchEndpoint"), QStringLiteral("videoId")},
    };
    for (const QStringList &path : paths) {
        QJsonValue cursor = renderer;
        for (const QString &step : path) {
            if (!cursor.isObject())
                break;
            cursor = cursor.toObject().value(step);
        }
        if (cursor.isString() && !cursor.toString().isEmpty())
            return cursor.toString();
    }
    return {};
}

QString textOfColumn(const QJsonObject &renderer, int index)
{
    const QJsonArray columns = renderer.value(QStringLiteral("flexColumns")).toArray();
    if (index >= columns.size())
        return {};
    const QJsonObject column = columns.at(index).toObject()
                                   .value(QStringLiteral("musicResponsiveListItemFlexColumnRenderer")).toObject();
    QString text;
    for (const QJsonValue &run : column.value(QStringLiteral("text")).toObject().value(QStringLiteral("runs")).toArray())
        text += run.toObject().value(QStringLiteral("text")).toString();
    if (text.isEmpty())
        text = column.value(QStringLiteral("text")).toObject().value(QStringLiteral("simpleText")).toString();
    return text.trimmed();
}

// The subtitle column is human text: "Song • Artist • 3:41 • 1.2M plays", in
// any order and sometimes missing pieces. Pull the artist and the duration out of
// it instead of trusting a fixed position.
void splitSubtitle(const QString &subtitle, QString *artist, QString *duration)
{
    static const QRegularExpression timeRe(QStringLiteral(R"(^\d{1,2}:\d{2}(:\d{2})?$)"));
    const QStringList parts = subtitle.split(QStringLiteral(" • "), Qt::SkipEmptyParts);
    for (const QString &rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (timeRe.match(part).hasMatch()) {
            if (duration && duration->isEmpty())
                *duration = part;
            continue;
        }
        if (part.compare(QStringLiteral("Song"), Qt::CaseInsensitive) == 0 ||
            part.compare(QStringLiteral("Video"), Qt::CaseInsensitive) == 0 ||
            part.compare(QStringLiteral("Album"), Qt::CaseInsensitive) == 0 ||
            part.compare(QStringLiteral("Single"), Qt::CaseInsensitive) == 0 ||
            part.compare(QStringLiteral("EP"), Qt::CaseInsensitive) == 0 ||
            part.endsWith(QStringLiteral("plays"), Qt::CaseInsensitive) ||
            part.endsWith(QStringLiteral("views"), Qt::CaseInsensitive)) {
            continue;
        }
        if (artist && artist->isEmpty())
            *artist = part;
    }
}

void collectSongs(const QJsonValue &node, QVariantList *out)
{
    if (node.isObject()) {
        const QJsonObject object = node.toObject();
        const QJsonValue rendererValue = object.value(QStringLiteral("musicResponsiveListItemRenderer"));
        if (rendererValue.isObject()) {
            const QJsonObject renderer = rendererValue.toObject();
            const QString id = videoIdOf(renderer);
            if (!id.isEmpty()) {
                const QString title = textOfColumn(renderer, 0);
                QString artist;
                QString duration;
                splitSubtitle(textOfColumn(renderer, 1), &artist, &duration);

                QVariantMap track;
                track.insert(QStringLiteral("id"), id);
                track.insert(QStringLiteral("title"), title);
                track.insert(QStringLiteral("artist"), artist);
                track.insert(QStringLiteral("duration"), duration);
                track.insert(QStringLiteral("thumbnail"),
                             QStringLiteral("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id));
                track.insert(QStringLiteral("url"), QStringLiteral("https://music.youtube.com/watch?v=") + id);
                out->append(track);
            }
        }
        for (auto it = object.begin(); it != object.end(); ++it)
            collectSongs(it.value(), out);
    } else if (node.isArray()) {
        for (const QJsonValue &value : node.toArray())
            collectSongs(value, out);
    }
}

// Omarchy-style flat TOML, read with a line parser for the one key we need.
QString readKey(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QRegularExpression re(
        QStringLiteral(R"re(^\s*%1\s*=\s*"([^"]*)"\s*$)re").arg(QRegularExpression::escape(key)));
    while (!file.atEnd()) {
        const auto match = re.match(QString::fromUtf8(file.readLine()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return {};
}

}  // namespace

YtMusic::YtMusic(QObject *parent, const QString &binary)
    : QObject(parent), m_binary(binary.isEmpty() ? QStringLiteral("yt-dlp") : binary)
{
    connect(&m_ytdlp, &QProcess::readyReadStandardOutput, this, [this] {
        m_ytdlpOutput.append(m_ytdlp.readAllStandardOutput());
    });
    connect(&m_ytdlp, &QProcess::readyReadStandardError, this, [this] {
        m_ytdlpOutput.append(m_ytdlp.readAllStandardError());
    });
    connect(&m_ytdlp, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                const QString output = QString::fromUtf8(m_ytdlpOutput);
                emit busyChanged();
                if (code != 0) {
                    fail(tr("%1 failed: %2").arg(m_currentRequest, output.trimmed().right(400)));
                    return;
                }
                QVariantList tracks;
                const auto lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString &line : lines) {
                    const auto fields = line.split(QLatin1Char('\t'));
                    if (fields.size() < 2)
                        continue;
                    const QString id = fields.at(0).trimmed();
                    if (id.isEmpty() || id == QStringLiteral("NA"))
                        continue;  // browse entries have no id until resolved
                    QVariantMap track;
                    track.insert(QStringLiteral("id"), id);
                    track.insert(QStringLiteral("title"), fields.at(1).trimmed());
                    track.insert(QStringLiteral("artist"), fields.size() > 2 ? fields.at(2).trimmed() : QString());
                    track.insert(QStringLiteral("duration"), fields.size() > 3 ? fields.at(3).trimmed() : QString());
                    track.insert(QStringLiteral("thumbnail"),
                                 QStringLiteral("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id));
                    track.insert(QStringLiteral("url"), QStringLiteral("https://music.youtube.com/watch?v=") + id);
                    tracks.append(track);
                }
                if (tracks.isEmpty())
                    fail(tr("%1 returned nothing").arg(m_currentRequest));
                else
                    emit tracksReady(tracks);
                m_ytdlpOutput.clear();
                m_currentRequest.clear();
            });

    QString kind;
    m_cookieSource = resolveCookieSource(&kind);
    m_cookieKind = kind;
}

YtMusic::~YtMusic()
{
    if (m_ytdlp.state() != QProcess::NotRunning) {
        m_ytdlp.kill();
        m_ytdlp.waitForFinished(500);
    }
}

QString YtMusic::resolveCookieSource(QString *kind)
{
    const QString cookiesFile = QDir::homePath() + kCookiesFile;
    if (QFileInfo::exists(cookiesFile)) {
        if (kind)
            *kind = QStringLiteral("file");
        return cookiesFile;
    }
    const QString own = readKey(QDir::homePath() + QStringLiteral("/.config/huruwave/config.toml"),
                               QStringLiteral("cookies_from"));
    if (!own.isEmpty()) {
        if (kind)
            *kind = QStringLiteral("browser");
        return own;
    }
    const QString inherited = readKey(QDir::homePath() + QStringLiteral("/.config/cliamp/config.toml"),
                                      QStringLiteral("cookies_from"));
    if (!inherited.isEmpty()) {
        if (kind)
            *kind = QStringLiteral("browser");
        return inherited;
    }
    if (kind)
        *kind = QString();
    return {};
}

void YtMusic::fail(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    emit failed(message);
}

void YtMusic::search(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;
    m_pendingQuery = trimmed;
    emit busyChanged();
    if (m_credentialsKey.isEmpty() || m_clientVersion.isEmpty())
        fetchCredentials();
    else
        performSearch(trimmed);
}

// The key and client version are constants of the current music.youtube.com
// bundle, so they are read from it rather than hardcoded -- hardcoding them is
// how a client silently starts failing months later.
void YtMusic::fetchCredentials()
{
    QNetworkRequest request{QUrl(kMusicHome)};
    // A browser-like agent AND explicit redirect following: music.youtube.com
    // answers a bare client with a consent redirect whose target has no API key
    // in it, which looks exactly like "the key changed" if you only report that.
    request.setHeader(QNetworkRequest::UserAgentHeader, kBrowserAgent);
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setRawHeader("Cookie", kConsentCookie);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit busyChanged();
            fail(tr("could not reach music.youtube.com: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray body = reply->readAll();
        const QString html = QString::fromUtf8(body);
        static const QRegularExpression keyRe(QStringLiteral(R"re("INNERTUBE_API_KEY":"([^"]+)")re"));
        static const QRegularExpression versionRe(QStringLiteral(R"re("INNERTUBE_CLIENT_VERSION":"([^"]+)")re"));
        const auto keyMatch = keyRe.match(html);
        const auto versionMatch = versionRe.match(html);
        if (!keyMatch.hasMatch() || !versionMatch.hasMatch()) {
            emit busyChanged();
            fail(tr("music.youtube.com served no API key (HTTP %1, %2 bytes, final URL %3)")
                     .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                     .arg(body.size())
                     .arg(reply->url().toString()));
            return;
        }
        m_credentialsKey = keyMatch.captured(1);
        m_clientVersion = versionMatch.captured(1);
        performSearch(m_pendingQuery);
    });
}

void YtMusic::performSearch(const QString &query)
{
    QJsonObject client;
    client.insert(QStringLiteral("clientName"), kClientName);
    client.insert(QStringLiteral("clientVersion"), m_clientVersion);
    client.insert(QStringLiteral("hl"), QStringLiteral("en"));
    client.insert(QStringLiteral("gl"), QStringLiteral("US"));

    QJsonObject context;
    context.insert(QStringLiteral("client"), client);

    QJsonObject body;
    body.insert(QStringLiteral("context"), context);
    body.insert(QStringLiteral("query"), query);

    QUrl url(QStringLiteral("https://music.youtube.com/youtubei/v1/search"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("key"), m_credentialsKey);
    params.addQueryItem(QStringLiteral("prettyPrint"), QStringLiteral("false"));
    url.setQuery(params);

    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, kBrowserAgent);
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setRawHeader("Cookie", kConsentCookie);

    m_searchReply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_searchReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_searchReply;
        m_searchReply = nullptr;
        reply->deleteLater();
        emit busyChanged();
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("search failed: %1").arg(reply->errorString()));
            return;
        }
        const QVariantList tracks = parseSearchResponse(reply->readAll());
        if (tracks.isEmpty())
            fail(tr("search returned nothing for \"%1\"").arg(m_pendingQuery));
        else
            emit tracksReady(tracks);
    });
}

QVariantList YtMusic::parseSearchResponse(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    QVariantList tracks;
    collectSongs(root, &tracks);
    return tracks;
}

QStringList YtMusic::cookieArguments() const
{
    QStringList args;
    if (m_cookieSource.isEmpty())
        return args;
    if (m_cookieKind == QStringLiteral("file"))
        args << QStringLiteral("--cookies") << m_cookieSource;
    else
        args << QStringLiteral("--cookies-from-browser") << m_cookieSource;
    return args;
}

void YtMusic::runYtDlp(const QStringList &extraArguments, const QString &what)
{
    if (m_ytdlp.state() != QProcess::NotRunning) {
        fail(tr("still working on: %1").arg(m_currentRequest));
        return;
    }
    m_ytdlpOutput.clear();
    m_lastError.clear();
    m_currentRequest = what;
    emit lastErrorChanged();

    QStringList args{QStringLiteral("--flat-playlist"), QStringLiteral("--no-warnings"),
                     QStringLiteral("--print"), QStringLiteral("%(id)s\t%(title)s\t%(uploader,channel)s\t%(duration)s")};
    args += cookieArguments();
    args += extraArguments;

    m_ytdlp.setProgram(m_binary);
    m_ytdlp.setArguments(args);
    m_ytdlp.start();
    emit busyChanged();
}

void YtMusic::loadPlaylist(const QString &playlistId)
{
    QString id = playlistId.trimmed();
    if (id.isEmpty())
        return;
    // Accept both a bare id (LM, WL, a playlist id) and a full URL.
    const QString url = id.startsWith(QStringLiteral("http"))
                            ? id
                            : QStringLiteral("https://music.youtube.com/playlist?list=") + id;
    runYtDlp({url}, tr("load %1").arg(id));
}

void YtMusic::loadUrl(const QString &url)
{
    if (url.trimmed().isEmpty())
        return;
    runYtDlp({url.trimmed()}, tr("load a URL"));
}
