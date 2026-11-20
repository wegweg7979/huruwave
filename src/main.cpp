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

#include "player.h"
#include "theme.h"
#include "visualizer.h"
#include "ytmusic.h"

namespace {

// Headless search, so the whole path (API credentials, request, parsing) can be
// driven from a shell and checked by an agent -- the pattern Omarchy's own apps
// use. Exits 0 or 1 with the error on stderr.
int runHeadlessSearch(YtMusic &provider, const QString &query, bool asJson)
{
    int exitCode = 1;
    QObject::connect(&provider, &YtMusic::tracksReady, &provider,
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
    QObject::connect(&provider, &YtMusic::failed, &provider, [&](const QString &message) {
        QTextStream(stderr) << "error: " << message << Qt::endl;
        exitCode = 1;
        QCoreApplication::quit();
    });

    // Ask once the event loop is running, so the reply has somewhere to land.
    QTimer::singleShot(0, &provider, [&provider, query] { provider.search(query); });
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
                                   QStringLiteral("search YouTube Music and print the results"), QStringLiteral("query"));
    QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("with --search: print JSON"));
    QCommandLineOption noWindowOption(QStringLiteral("no-window"),
                                      QStringLiteral("with --search: do not open the window"));
    parser.addOption(searchOption);
    parser.addOption(jsonOption);
    parser.addOption(noWindowOption);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("a file or URL to play, or a URL to queue"));
    parser.process(app);

    Theme theme;
    YtMusic provider;
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

    const QString query = parser.value(searchOption).trimmed();
    if (!query.isEmpty() && parser.isSet(noWindowOption))
        return runHeadlessSearch(provider, query, parser.isSet(jsonOption));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("player"), &player);
    engine.rootContext()->setContextProperty(QStringLiteral("ytmusic"), &provider);
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

    // A URL passed on the command line plays immediately; the model needs to know
    // so the now-playing panel is not stuck on "Nothing playing" while audio is
    // coming out of the speakers.
    engine.rootContext()->setContextProperty(QStringLiteral("initialUrl"),
                                             positional.isEmpty() ? QString() : positional.first());

    return app.exec();
}
