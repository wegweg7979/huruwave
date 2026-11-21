// huruwave — an Omarchy-native music player: YouTube Music, Apple Music catalog
// lookups and the local library, in one window that follows the desktop theme.

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>

#include <functional>

#include "applemusic.h"
#include "applesession.h"
#include "player.h"
#include "theme.h"
#include "visualizer.h"
#include "widevine.h"
#include "ytmusic.h"

#ifdef HURUWAVE_APPLE_WEB
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QtWebEngineQuick/QQuickWebEngineProfile>
#include <QtWebEngineQuick/QtWebEngineQuick>
#endif

namespace {

#ifdef HURUWAVE_APPLE_WEB

// The DRM surface's web profile, built here rather than in the QML file: the surface
// BORROWS the user's Apple session, and injecting cookies needs QtWebEngine's C++
// cookie store — which QML does not expose. The profile is handed to the QML view as
// a context property (AppleWeb.qml's `profile:`), created before any view exists.
//
// Apple answers QtWebEngine's own user agent with an "unsupported browser" page, so
// a Chrome string goes out — with the engine's own Chromium major, because a UA that
// disagrees with the client hints (UA says 124, hints say 140) is exactly the kind of
// inconsistency a web player mishandles.
QQuickWebEngineProfile *makeAppleSurfaceProfile(QObject *parent)
{
    auto *profile = new QQuickWebEngineProfile(QStringLiteral("huruwave-apple"), parent);
    profile->setPersistentCookiesPolicy(QQuickWebEngineProfile::ForcePersistentCookies);
    profile->setHttpUserAgent(
        QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
                       "Chrome/140.0.0.0 Safari/537.36"));
    return profile;
}

#endif

// Headless run, so the whole path (credentials, request, parsing) can be driven
// from a shell and checked by an agent -- the pattern Omarchy's own apps use.
// Exits 0 or 1 with the error on stderr. The provider is a template because both
// catalogs expose the same two signals, and the action is passed in so one
// function serves a search and a playlist load.
template <typename Provider>
int runHeadless(Provider &provider, const std::function<void(Provider &)> &launch, bool asJson)
{
    int exitCode = 1;
    QObject::connect(&provider, &Provider::tracksReady, &provider,
                     [&](const QVariantList &tracks) {
                         if (asJson) {
                             QJsonArray array;
                             for (const QVariant &entry : tracks)
                                 array.append(QJsonObject::fromVariantMap(entry.toMap()));
                             QTextStream(stdout) << QJsonDocument(array).toJson(QJsonDocument::Compact) << Qt::endl;
                         } else {
                             for (const QVariant &entry : tracks) {
                                 const QVariantMap map = entry.toMap();
                                 QTextStream(stdout)
                                     << map.value(QStringLiteral("title")).toString() << " | "
                                     << map.value(QStringLiteral("artist")).toString() << " | "
                                     << map.value(QStringLiteral("duration")).toString() << " | "
                                     << map.value(QStringLiteral("url")).toString() << Qt::endl;
                             }
                         }
                         exitCode = 0;
                         QCoreApplication::quit();
                     });
    QObject::connect(&provider, &Provider::failed, &provider, [&](const QString &message) {
        QTextStream(stderr) << "error: " << message << Qt::endl;
        exitCode = 1;
        QCoreApplication::quit();
    });

    // Ask once the event loop is running, so the reply has somewhere to land.
    QTimer::singleShot(0, &provider, [&provider, &launch] { launch(provider); });
    QCoreApplication::exec();
    return exitCode;
}

}  // namespace

int main(int argc, char *argv[])
{
    // Whether this build carries the DRM surface at all. A build without it is a
    // perfectly good player — it just points at the browser for full Apple tracks
    // instead of opening them in-window.
#ifdef HURUWAVE_APPLE_WEB
    constexpr bool builtWithAppleWeb = true;
#else
    constexpr bool builtWithAppleWeb = false;
#endif

    // The Widevine CDM is looked for HERE, before anything is constructed: Qt
    // WebEngine reads --widevine-path out of QTWEBENGINE_CHROMIUM_FLAGS when it
    // initialises, which is earlier than the command line is parsed (and earlier
    // than the window exists). The result is kept for --widevine-status and for
    // the "can this machine play full tracks" answer the QML asks for.
    const Widevine::Cdm widevine = Widevine::find();

    // The Apple session this machine's browser has (bin/apple-session writes it).
    // Read here, once: --widevine-status reports it, and the surface's profile is
    // given its cookies. A missing jar is not an error — the surface then shows
    // Apple's page unsigned, which is how this behaved before any of it existed.
    const AppleSession::Jar appleSession = AppleSession::readNetscapeFile(AppleSession::defaultPath());

#ifdef HURUWAVE_APPLE_WEB
    if (widevine.isValid()) {
        const QString flag = QStringLiteral("--widevine-path=%1").arg(widevine.path);
        QString flags = qEnvironmentVariable("QTWEBENGINE_CHROMIUM_FLAGS");
        // Append rather than overwrite: the variable is a documented user knob, and
        // a user who set it (or who already passed widevine-path) wins.
        if (!flags.contains(QStringLiteral("widevine-path"))) {
            if (!flags.isEmpty())
                flags += QLatin1Char(' ');
            flags += flag;
            qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.toUtf8());
        }
    }
    // Both must happen before the QGuiApplication exists, per Qt WebEngine's own
    // requirements: one shared OpenGL context, and the Quick initialisation.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QtWebEngineQuick::initialize();
#endif

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("huruwave"));
    app.setApplicationDisplayName(QStringLiteral("huruwave"));
    app.setOrganizationName(QStringLiteral("hurunagus"));

    // Ties the Wayland app_id to huruwave.desktop so the compositor and the
    // launcher find the right icon and window rules.
    app.setDesktopFileName(QStringLiteral("huruwave"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("huruwave")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("An Omarchy-native music player."));
    parser.addHelpOption();
    QCommandLineOption searchOption({QStringLiteral("s"), QStringLiteral("search")},
                                   QStringLiteral("search and print the results"), QStringLiteral("query"));
    QCommandLineOption providerOption(QStringLiteral("provider"),
                                      QStringLiteral("which catalog to search: ytmusic (default) or apple"),
                                      QStringLiteral("name"), QStringLiteral("ytmusic"));
    QCommandLineOption playlistOption(QStringLiteral("playlist"),
                                      QStringLiteral("load one of your playlists by id "
                                                     "(LM = Liked Music, WL = Watch Later)"),
                                      QStringLiteral("id"));
    parser.addOption(playlistOption);
    QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("with --search: print JSON"));
    QCommandLineOption cookiesStatusOption(
        QStringLiteral("cookies-status"),
        QStringLiteral("print which cookie source was found and what it can do, then exit "
                       "(0 when a usable cookie source was found)"));
    parser.addOption(cookiesStatusOption);
    QCommandLineOption widevineStatusOption(
        QStringLiteral("widevine-status"),
        QStringLiteral("print whether this build can play DRM (Apple Music) full tracks in-window "
                       "and which Widevine CDM it found, then exit (0 when it can)"));
    parser.addOption(widevineStatusOption);
    QCommandLineOption appleWebOption(
        QStringLiteral("apple-web"),
        QStringLiteral("open the Apple Music DRM surface on this URL at startup (a music.apple.com "
                       "track, album or playlist URL)"),
        QStringLiteral("url"));
    parser.addOption(appleWebOption);
    QCommandLineOption noWindowOption(QStringLiteral("no-window"),
                                      QStringLiteral("with --search: do not open the window"));
    parser.addOption(searchOption);
    parser.addOption(providerOption);
    parser.addOption(jsonOption);
    parser.addOption(noWindowOption);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("a file or URL to play, or a URL to queue"));
    parser.process(app);

    Theme theme;
    YtMusic provider;
    AppleMusic apple;
    Player player;
    Visualizer visualizer;

    // The cookie source is decided once and given to BOTH the provider and the
    // engine: mpv plays YouTube Music URLs itself (through yt-dlp), so it needs
    // the same cookies the provider browses with.
    QString kind;
    const QString source = YtMusic::resolveCookieSource(&kind);
    if (kind == QStringLiteral("file"))
        player.setCookieSource(source, {});
    else
        player.setCookieSource({}, source);

    if (parser.isSet(cookiesStatusOption)) {
        const QString status = YtMusic::describeCookieSource(source, kind);
        QTextStream(stdout) << "source: " << (source.isEmpty() ? QStringLiteral("(none)") : source)
                            << Qt::endl
                            << "kind:   " << (kind.isEmpty() ? QStringLiteral("(none)") : kind)
                            << Qt::endl
                            << "status: " << status << Qt::endl;
        // 0 when the app will actually send credentials: a cookies file holding a
        // real session, or a browser store yt-dlp can read. A session-less file is
        // a 1 even though it exists, because account views fail with it.
        const bool usable = status.startsWith(QStringLiteral("signed in"))
                            || kind == QStringLiteral("browser");
        if (kind == QStringLiteral("browser"))
            QTextStream(stdout) << "note:   a browser store is chosen; verify it live with "
                                   "`huruwave --playlist LM --no-window`" << Qt::endl;
        return usable ? 0 : 1;
    }

    // The DRM surface's own status, in the same shape as --cookies-status: what
    // this build can do, and what it would use to do it.
    if (parser.isSet(widevineStatusOption)) {
        const bool ready = builtWithAppleWeb && widevine.isValid();
        QTextStream(stdout)
            << "apple-web: " << (builtWithAppleWeb ? QStringLiteral("built in (QtWebEngine)")
                                                   : QStringLiteral("NOT built in (HURUWAVE_APPLE_WEB=OFF)"))
            << Qt::endl
            << "widevine:  " << Widevine::describe(widevine) << Qt::endl
            << "cdm-path:  "
            << (widevine.isValid() ? widevine.path : QStringLiteral("(none)")) << Qt::endl
            << "cdm-from:  "
            << (widevine.isValid() ? widevine.origin : QStringLiteral("(none)")) << Qt::endl
            << "apple-ssn: " << AppleSession::describe(appleSession) << Qt::endl
            << "status:    "
            << (ready ? QStringLiteral("full Apple Music tracks can play in-window (F)")
                      : (builtWithAppleWeb
                             ? QStringLiteral("no CDM on this machine — full tracks open in the browser")
                             : QStringLiteral("this build has no DRM surface — full tracks open in the browser")))
            << Qt::endl;
        return ready ? 0 : 1;
    }

    const QString query = parser.value(searchOption).trimmed();
    const QString playlist = parser.value(playlistOption).trimmed();
    const bool appleWanted = parser.value(providerOption).trimmed().toLower() == QStringLiteral("apple");
    const bool asJson = parser.isSet(jsonOption);
    if (!query.isEmpty() && parser.isSet(noWindowOption)) {
        const QString wanted = query;
        return appleWanted
                   ? runHeadless<AppleMusic>(apple, [wanted](AppleMusic &p) { p.search(wanted); }, asJson)
                   : runHeadless<YtMusic>(provider, [wanted](YtMusic &p) { p.search(wanted); }, asJson);
    }
    if (!playlist.isEmpty() && parser.isSet(noWindowOption)) {
        // Loading a playlist is the one thing here that needs the session cookies,
        // so it doubles as the check that the account actually works.
        const QString wanted = playlist;
        return runHeadless<YtMusic>(provider, [wanted](YtMusic &p) { p.loadPlaylist(wanted); }, asJson);
    }

    QQmlApplicationEngine engine;

#ifdef HURUWAVE_APPLE_WEB
    // The surface's profile and its borrowed session, both before any view exists.
    // QtWebEngine's cookie store takes the cookies and encrypts them itself on the
    // way to disk, so nothing here handles the keyring.
    QQuickWebEngineProfile *appleProfile = makeAppleSurfaceProfile(&app);
    for (const QNetworkCookie &cookie : appleSession.cookies)
        appleProfile->cookieStore()->setCookie(cookie);
    engine.rootContext()->setContextProperty(QStringLiteral("appleProfile"), appleProfile);
#endif

    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("player"), &player);
    engine.rootContext()->setContextProperty(QStringLiteral("ytmusic"), &provider);
    engine.rootContext()->setContextProperty(QStringLiteral("apple"), &apple);
    engine.rootContext()->setContextProperty(QStringLiteral("visualizer"), &visualizer);
    // What the window needs to know about the DRM surface: whether it exists here
    // (build AND machine), and what it would use. QML only ever opens the surface
    // when this is true, so an ordinary build keeps the external-browser path.
    engine.rootContext()->setContextProperty(QStringLiteral("appleWebAvailable"),
                                             builtWithAppleWeb && widevine.isValid());
    engine.rootContext()->setContextProperty(QStringLiteral("appleWebCdm"),
                                             Widevine::describe(widevine));

    // The analyser listens to the OUTPUT device, so it runs only while something
    // is playing: pausing keeps it alive (real silence is a flat line, which is
    // honest and avoids respawning a process every pause), and it stops when the
    // engine goes idle and with the app. A music player that keeps an FFT running
    // behind an idle window is just burning battery.
    //
    // That decision lives in QML now, because mpv is no longer the only engine that
    // can make sound — but the analyser only follows audio the user can SEE: the DRM
    // surface covers the whole window while it is open, so the bars (and cava with
    // them) have nothing to show then and Main.qml turns them off. See Main.qml's
    // `audioActive`.
    // A search given on the command line is run by the window once it exists,
    // which makes `huruwave --search 'boards of canada'` a usable demo.
    engine.rootContext()->setContextProperty(QStringLiteral("initialQuery"), query);
    engine.rootContext()->setContextProperty(QStringLiteral("initialProvider"),
                                             appleWanted ? QStringLiteral("apple")
                                                         : QStringLiteral("ytmusic"));
    engine.rootContext()->setContextProperty(QStringLiteral("initialPlaylist"), playlist);
    // --apple-web: the surface opens on this URL as soon as the window is up. It is
    // what makes the DRM path testable from a shell (and what a desktop file would
    // launch for "play the full track").
    engine.rootContext()->setContextProperty(QStringLiteral("initialAppleWeb"),
                                             parser.value(appleWebOption).trimmed());

    // `huruwave /path/to/file` (or a URL) plays straight away, which makes
    // testing and desktop-file launching do the obvious thing. The window is told
    // the URL too, so the now-playing panel reflects it.
    const QStringList positional = parser.positionalArguments();
    QString playable;
    if (!positional.isEmpty()) {
        const QString first = positional.first();
        if (first.startsWith(QStringLiteral("http")))
            playable = first;
        else if (QFileInfo::exists(first))
            playable = QUrl::fromLocalFile(QFileInfo(first).absoluteFilePath()).toString();
    }
    if (!playable.isEmpty())
        player.play(playable);

    engine.rootContext()->setContextProperty(QStringLiteral("initialUrl"), playable);

    // Loaded through the QML module, not by a qrc:/ URL: the module's files are
    // compiled at build time, which is what keeps a stale per-user QML cache from
    // serving the previous build's UI (see the comment in CMakeLists.txt).
    engine.loadFromModule("huruwave", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
