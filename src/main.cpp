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
#include "player.h"
#include "theme.h"
#include "visualizer.h"
#include "ytmusic.h"

namespace {

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
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("player"), &player);
    engine.rootContext()->setContextProperty(QStringLiteral("ytmusic"), &provider);
    engine.rootContext()->setContextProperty(QStringLiteral("apple"), &apple);
    engine.rootContext()->setContextProperty(QStringLiteral("visualizer"), &visualizer);

    // The analyser listens to the OUTPUT device, so it runs only while a stream
    // is loaded: pausing keeps it alive (real silence is a flat line, which is
    // honest and avoids respawning a process every pause), and it stops when the
    // engine goes idle and with the app. A music player that keeps an FFT running
    // behind an idle window is just burning battery.
    QObject::connect(&player, &Player::stateChanged, &visualizer, [&player, &visualizer] {
        visualizer.setActive(player.playing());
    });
    // A search given on the command line is run by the window once it exists,
    // which makes `huruwave --search 'boards of canada'` a usable demo.
    engine.rootContext()->setContextProperty(QStringLiteral("initialQuery"), query);
    engine.rootContext()->setContextProperty(QStringLiteral("initialProvider"),
                                             appleWanted ? QStringLiteral("apple")
                                                         : QStringLiteral("ytmusic"));
    engine.rootContext()->setContextProperty(QStringLiteral("initialPlaylist"), playlist);

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

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
