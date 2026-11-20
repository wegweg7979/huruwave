#include <QtTest>

#include <QDir>
#include <QFile>

#include "applemusic.h"
#include "applesession.h"
#include "config.h"
#include "theme.h"
#include "visualizer.h"
#include "widevine.h"
#include "ytmusic.h"

// The things tested here are the ones that fail silently in production: the
// Omarchy theme parser (a wrong colour is invisible), the YouTube Music search
// parser (a wrong field just shows an empty row), the Apple Music mapping (an
// upscale that never happens, or a preview that is offered when there is none),
// and the cava frame parser (a format change would look like the visualiser
// dying).
class HuruwaveTests : public QObject {
    Q_OBJECT

private slots:
    void parsesOmarchyColors();
    void survivesAMissingTheme();
    void readsQuotedAndBareValues();
    void parsesYtMusicSearchPayload();
    void parsesAppleMusicSearchPayload();
    void describesCookieSessions();
    void readsConfigValues();
    void parsesCavaFrames();
    void dropsGarbledCavaFrames();
    void findsTheWidevineCdm();
    void readsTheAppleSessionJar();
};

void HuruwaveTests::parsesOmarchyColors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("colors.toml")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("mode = \"dark\"\n\naccent = \"#7aa2f7\"\nbackground = \"#1a1b26\"\n");
    file.close();

    const auto colors = Theme::readColors(file.fileName());
    QCOMPARE(colors.value(QStringLiteral("accent")), QStringLiteral("#7aa2f7"));
    QCOMPARE(colors.value(QStringLiteral("background")), QStringLiteral("#1a1b26"));
    // `mode` is not a colour but is still a key; it must not break parsing.
    QCOMPARE(colors.value(QStringLiteral("mode")), QStringLiteral("dark"));
}

void HuruwaveTests::survivesAMissingTheme()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto colors = Theme::readColors(dir.filePath(QStringLiteral("nope.toml")));
    QVERIFY(colors.isEmpty());

    // A Theme built on an empty directory must still expose usable defaults,
    // because the app has to start on a machine where Omarchy is not installed.
    Theme theme(dir.path());
    QVERIFY(!theme.accent().isEmpty());
    QVERIFY(theme.accent().startsWith(QLatin1Char('#')));
}

void HuruwaveTests::readsQuotedAndBareValues()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("colors.toml")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    // Comments, blank lines and spacing must not trip the parser.
    file.write("# a comment\n\n  accent=\"#abcdef\"\n  muted = \"#414868\"  \n");
    file.close();

    const auto colors = Theme::readColors(file.fileName());
    QCOMPARE(colors.value(QStringLiteral("accent")), QStringLiteral("#abcdef"));
    QCOMPARE(colors.value(QStringLiteral("muted")), QStringLiteral("#414868"));
}

namespace {

// The payload is BUILT rather than written as a JSON string: a hand-written
// fixture is one brace away from testing nothing, and its syntax errors look like
// parser bugs (which is exactly what happened the first time).
//
// Note the separators: a live music.youtube.com response puts " • " in its own
// run between the fields, which is why the parser may join runs without adding
// one. A fixture without them produces "SongBoards of Canada6:06".
QJsonObject textBlock(const QStringList &texts)
{
    QJsonArray runs;
    for (int i = 0; i < texts.size(); ++i) {
        if (i > 0)
            runs.append(QJsonObject{{QStringLiteral("text"), QStringLiteral(" • ")}});
        runs.append(QJsonObject{{QStringLiteral("text"), texts.at(i)}});
    }
    return QJsonObject{{QStringLiteral("text"), QJsonObject{{QStringLiteral("runs"), runs}}}};
}

QJsonObject flexColumn(const QStringList &texts)
{
    return QJsonObject{{QStringLiteral("musicResponsiveListItemFlexColumnRenderer"), textBlock(texts)}};
}

}  // namespace

void HuruwaveTests::parsesYtMusicSearchPayload()
{
    // Shape 1: artist and duration in the subtitle column, id on playlistItemData.
    const QJsonObject songRow{
        {QStringLiteral("playlistItemData"),
         QJsonObject{{QStringLiteral("videoId"), QStringLiteral("4CE29HLQSnk")}}},
        {QStringLiteral("flexColumns"),
         QJsonArray{flexColumn({QStringLiteral("Twoism")}),
                    flexColumn({QStringLiteral("Song"), QStringLiteral("Boards of Canada"),
                                QStringLiteral("6:06")})}}};

    // Shape 2: no artist at all, and the id only reachable through the
    // play-button overlay.
    const QJsonObject watchEndpoint{{QStringLiteral("watchEndpoint"),
                                     QJsonObject{{QStringLiteral("videoId"), QStringLiteral("x0mhZHOfKj8")}}}};
    const QJsonObject playButton{{QStringLiteral("musicPlayButtonRenderer"),
                                  QJsonObject{{QStringLiteral("playNavigationEndpoint"), watchEndpoint}}}};
    const QJsonObject overlayContent{{QStringLiteral("content"), playButton}};
    const QJsonObject overlay{{QStringLiteral("musicItemThumbnailOverlayRenderer"), overlayContent}};

    const QJsonObject videoRow{
        {QStringLiteral("overlay"), overlay},
        {QStringLiteral("flexColumns"),
         QJsonArray{flexColumn({QStringLiteral("Beware The Friendly Stranger")}),
                    flexColumn({QStringLiteral("Song"), QStringLiteral("0:38"),
                                QStringLiteral("832K plays")})}}};

    const QJsonArray rows{QJsonObject{{QStringLiteral("musicResponsiveListItemRenderer"), songRow}},
                          QJsonObject{{QStringLiteral("musicResponsiveListItemRenderer"), videoRow}}};
    const QJsonObject shelf{{QStringLiteral("contents"), rows}};
    const QJsonObject sectionList{{QStringLiteral("contents"), QJsonArray{shelf}}};
    const QJsonObject tabContent{{QStringLiteral("content"),
                                  QJsonObject{{QStringLiteral("sectionListRenderer"), sectionList}}}};
    const QJsonObject tabRenderer{{QStringLiteral("tabRenderer"), tabContent}};
    const QJsonObject tabbed{{QStringLiteral("tabs"), QJsonArray{tabRenderer}}};
    const QJsonObject payload{
        {QStringLiteral("contents"),
         QJsonObject{{QStringLiteral("tabbedSearchResultsRenderer"), tabbed}}}};

    const QByteArray encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QVERIFY(!encoded.isEmpty());

    const QVariantList tracks = YtMusic::parseSearchResponse(encoded);
    QCOMPARE(tracks.size(), 2);

    const QVariantMap result0 = tracks.at(0).toMap();
    QCOMPARE(result0.value(QStringLiteral("id")).toString(), QStringLiteral("4CE29HLQSnk"));
    QCOMPARE(result0.value(QStringLiteral("title")).toString(), QStringLiteral("Twoism"));
    QCOMPARE(result0.value(QStringLiteral("artist")).toString(), QStringLiteral("Boards of Canada"));
    QCOMPARE(result0.value(QStringLiteral("duration")).toString(), QStringLiteral("6:06"));
    QCOMPARE(result0.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://music.youtube.com/watch?v=4CE29HLQSnk"));

    const QVariantMap result1 = tracks.at(1).toMap();
    QCOMPARE(result1.value(QStringLiteral("id")).toString(), QStringLiteral("x0mhZHOfKj8"));
    QCOMPARE(result1.value(QStringLiteral("title")).toString(), QStringLiteral("Beware The Friendly Stranger"));
    // "Song" and "832K plays" must not be mistaken for the artist.
    QCOMPARE(result1.value(QStringLiteral("artist")).toString(), QString());
    QCOMPARE(result1.value(QStringLiteral("duration")).toString(), QStringLiteral("0:38"));

    // Garbage in must not produce phantom tracks.
    QVERIFY(YtMusic::parseSearchResponse(QByteArrayLiteral("not json")).isEmpty());
    QVERIFY(YtMusic::parseSearchResponse(QByteArrayLiteral("{}")).isEmpty());
}

void HuruwaveTests::parsesCavaFrames()
{
    // cava's raw ascii frame: values 0..100, ';' between them and a trailing
    // delimiter before the newline (that empty last field is normal).
    const QVector<double> frame = Visualizer::parseFrame(QByteArrayLiteral("0;50;100;25;"));
    QCOMPARE(frame.size(), 4);
    QCOMPARE(frame.at(0), 0.0);
    QCOMPARE(frame.at(1), 0.5);
    QCOMPARE(frame.at(2), 1.0);
    QCOMPARE(frame.at(3), 0.25);

    // Out-of-range values are clamped: a bar cannot overshoot the panel.
    const QVector<double> clamped = Visualizer::parseFrame(QByteArrayLiteral("120;-5;"));
    QCOMPARE(clamped.size(), 2);
    QCOMPARE(clamped.at(0), 1.0);
    QCOMPARE(clamped.at(1), 0.0);

    // A silent frame is still a frame: all zeros, not an empty list, or the
    // panel would flip to the fallback animation during every quiet passage.
    const QVector<double> silence = Visualizer::parseFrame(QByteArrayLiteral("0;0;0;"));
    QCOMPARE(silence.size(), 3);
    QCOMPARE(silence.at(1), 0.0);
}

void HuruwaveTests::dropsGarbledCavaFrames()
{
    // A half-written or non-numeric frame is dropped whole. Drawing part of one
    // would show a bar pattern that never existed.
    QVERIFY(Visualizer::parseFrame(QByteArray()).isEmpty());
    QVERIFY(Visualizer::parseFrame(QByteArrayLiteral(";;;")).isEmpty());
    QVERIFY(Visualizer::parseFrame(QByteArrayLiteral("12;oops;30;")).isEmpty());

    // A monitor source is the default sink's name plus ".monitor", and the
    // analyser refuses to run without one -- a microphone-reactive "visualiser"
    // is worse than none.
    const QString source = Visualizer::defaultMonitorSource();
    if (!source.isEmpty())
        QVERIFY(source.endsWith(QStringLiteral(".monitor")));
}

// Apple's catalog answer, mapped with no network. Two things matter beyond the
// field names: the artwork must come back upscaled (the API serves a 100 px
// thumbnail by default) and `playable` must reflect whether Apple actually
// published a preview, because that is what the UI offers to play.
void HuruwaveTests::parsesAppleMusicSearchPayload()
{
    const QJsonObject withPreview{
        {QStringLiteral("trackName"), QStringLiteral("Turquoise Hexagon Sun")},
        {QStringLiteral("artistName"), QStringLiteral("Boards of Canada")},
        {QStringLiteral("collectionName"), QStringLiteral("Music Has the Right to Children")},
        {QStringLiteral("trackId"), 281116078},
        {QStringLiteral("trackTimeMillis"), 307880},
        {QStringLiteral("artworkUrl100"),
         QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Music/abc/100x100bb.jpg")},
        {QStringLiteral("previewUrl"), QStringLiteral("https://audio-ssl.itunes.apple.com/AudioPreview211/x.m4a")},
        {QStringLiteral("trackViewUrl"), QStringLiteral("https://music.apple.com/gb/album/x/281116024?i=281116078")}};

    // A collection shares this endpoint and has no trackName: it must be skipped
    // rather than rendered as an empty row.
    const QJsonObject collection{{QStringLiteral("collectionName"), QStringLiteral("Inferno")},
                                 {QStringLiteral("collectionId"), 1890015523}};

    // A track with no previewUrl is listed but not playable.
    const QJsonObject noPreview{
        {QStringLiteral("trackName"), QStringLiteral("Father And Son")},
        {QStringLiteral("artistName"), QStringLiteral("Boards of Canada")},
        {QStringLiteral("trackTimeMillis"), 204835},
        {QStringLiteral("artworkUrl100"), QStringLiteral("https://is1-ssl.mzstatic.com/thumb/100x100bb.jpg")}};

    const QJsonObject payload{{QStringLiteral("resultCount"), 3},
                              {QStringLiteral("results"), QJsonArray{withPreview, collection, noPreview}}};
    const QByteArray encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    const QVariantList tracks = AppleMusic::parseResults(encoded);
    QCOMPARE(tracks.size(), 2);  // the collection is dropped

    const QVariantMap first = tracks.at(0).toMap();
    QCOMPARE(first.value("title").toString(), QStringLiteral("Turquoise Hexagon Sun"));
    QCOMPARE(first.value("artist").toString(), QStringLiteral("Boards of Canada"));
    QCOMPARE(first.value("album").toString(), QStringLiteral("Music Has the Right to Children"));
    QCOMPARE(first.value("duration").toString(), QStringLiteral("5:07"));  // from 307880 ms
    QCOMPARE(first.value("playable").toBool(), true);
    QCOMPARE(first.value("source").toString(), QStringLiteral("apple"));
    QVERIFY(first.value("thumbnail").toString().endsWith(QStringLiteral("600x600bb.jpg")));
    QVERIFY(!first.value("thumbnail").toString().contains(QStringLiteral("100x100")));
    QCOMPARE(first.value("url").toString(), first.value("url").toString());  // the preview
    QVERIFY(first.value("url").toString().contains(QStringLiteral("AudioPreview")));
    QVERIFY(first.value("webUrl").toString().startsWith(QStringLiteral("https://music.apple.com/")));

    const QVariantMap third = tracks.at(1).toMap();
    QCOMPARE(third.value("title").toString(), QStringLiteral("Father And Son"));
    QCOMPARE(third.value("duration").toString(), QStringLiteral("3:24"));
    QCOMPARE(third.value("playable").toBool(), false);
    QVERIFY(third.value("url").toString().isEmpty());

    QVERIFY(AppleMusic::parseResults(QByteArrayLiteral("not json")).isEmpty());
    QVERIFY(AppleMusic::parseResults(QByteArrayLiteral("{}")).isEmpty());
}

// A cookie file that exists is not a session. The file this check exists for held
// eleven visitor cookies and none of the account ones, while the UI said "signed
// in · cookie file" -- so the distinction is tested rather than assumed. The
// HttpOnly rows are the trap: they start with '#' but are cookies, not comments,
// and they are exactly the __Secure-*PSID ones.
void HuruwaveTests::describesCookieSessions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/cookies.txt");

    QVERIFY(YtMusic::describeCookieSource({}, {}).contains(QStringLiteral("no cookies")));

    {
        QFile empty(path);
        QVERIFY(empty.open(QIODevice::WriteOnly | QIODevice::Text));
        empty.write("# Netscape HTTP Cookie File\n");
        empty.close();
        QVERIFY(YtMusic::describeCookieSource(path, QStringLiteral("file")).contains(QStringLiteral("empty")));
    }

    // Visitor cookies only: present, non-empty, and still not a session.
    {
        QFile noSession(path);
        QVERIFY(noSession.open(QIODevice::WriteOnly | QIODevice::Text));
        noSession.write(".youtube.com\tTRUE\t/\tTRUE\t1795000000\tVISITOR_INFO1_LIVE\tvalue\n");
        noSession.write(".youtube.com\tTRUE\t/\tTRUE\t1795000000\tSOCS\tvalue\n");
        noSession.close();
        const QString described = YtMusic::describeCookieSource(path, QStringLiteral("file"));
        QVERIFY2(described.contains(QStringLiteral("no session")), qPrintable(described));
        QVERIFY(!described.startsWith(QStringLiteral("signed in")));
    }

    // Now the real thing, with the session cookie behind an HttpOnly marker.
    {
        QFile signedIn(path);
        QVERIFY(signedIn.open(QIODevice::WriteOnly | QIODevice::Text));
        signedIn.write(".youtube.com\tTRUE\t/\tTRUE\t1795000000\tVISITOR_INFO1_LIVE\tvalue\n");
        signedIn.write("#HttpOnly_.youtube.com\tTRUE\t/\tTRUE\t1795000000\t__Secure-1PSID\tvalue\n");
        signedIn.close();
        const QString described = YtMusic::describeCookieSource(path, QStringLiteral("file"));
        QVERIFY2(described.startsWith(QStringLiteral("signed in")), qPrintable(described));
        QVERIFY(described.contains(QStringLiteral("2 cookies")));
    }

    // A browser source cannot be read from here, and is not claimed to be either.
    const QString browser = YtMusic::describeCookieSource(QStringLiteral("firefox"), QStringLiteral("browser"));
    QVERIFY(browser.contains(QStringLiteral("firefox")));
    QVERIFY(!browser.startsWith(QStringLiteral("signed in")));
}

// The settings file also carries cookies_from, so the reader has to cope with the
// shapes a hand-edited line arrives in: bare values, quoted values, trailing
// comments, and keys that are not there at all.
void HuruwaveTests::readsConfigValues()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/config.toml");

    QVERIFY(Config::readFrom(path, QStringLiteral("cookies_from")).isEmpty());

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("# a comment line\n"
               "cookies_from = \"brave+gnomekeyring:/home/you/.config/BraveSoftware/Brave-Origin/Default\"\n"
               "commented = 0.5 # a trailing comment is not part of the value\n"
               "  spaced   =   bare-value  \n");
    file.close();

    QCOMPARE(Config::readFrom(path, QStringLiteral("cookies_from")),
             QStringLiteral("brave+gnomekeyring:/home/you/.config/BraveSoftware/Brave-Origin/Default"));
    QCOMPARE(Config::readFrom(path, QStringLiteral("commented")), QStringLiteral("0.5"));
    QCOMPARE(Config::readFrom(path, QStringLiteral("spaced")), QStringLiteral("bare-value"));
    QVERIFY(Config::readFrom(path, QStringLiteral("missing")).isEmpty());
    // A key that merely starts the same way must not match.
    QVERIFY(Config::readFrom(path, QStringLiteral("cook")).isEmpty());
}

namespace {

// The on-disk shape a Chromium profile has after its component updater downloads a
// CDM: <root>/<browser>/WidevineCdm/<version>/_platform_specific/linux_x64/
// libwidevinecdm.so. Returns the module path.
QString makeCdm(const QString &root, const QString &browser, const QString &version)
{
    const QString dir = root + QLatin1Char('/') + browser + QStringLiteral("/WidevineCdm/")
                        + version + QStringLiteral("/_platform_specific/linux_x64");
    if (!QDir().mkpath(dir))
        return {};
    const QString module = dir + QStringLiteral("/libwidevinecdm.so");
    QFile file(module);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write("not a real CDM; in this test only the path matters");
    file.close();
    return module;
}

}  // namespace

// The CDM finder decides whether a DRM-capable build can play Apple's full tracks
// at all, and it fails in a way nobody would notice — the panel silently goes back
// to pointing at the browser — so the rules are tested instead of eyeballed on the
// one machine the developer happens to use.
void HuruwaveTests::findsTheWidevineCdm()
{
    // An override left in the environment would make every case below pass for the
    // wrong reason.
    const QByteArray savedOverride = qgetenv("HURUWAVE_WIDEVINE");
    qunsetenv("HURUWAVE_WIDEVINE");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString oldCdm = makeCdm(dir.path(), QStringLiteral("browser-one"),
                                   QStringLiteral("4.10.3050.0"));
    const QString newCdm = makeCdm(dir.path(), QStringLiteral("browser-two"),
                                   QStringLiteral("4.10.3112.0"));
    QVERIFY(!oldCdm.isEmpty());
    QVERIFY(!newCdm.isEmpty());

    // The HIGHEST version wins: an old CDM the browser has already superseded must
    // not shadow the current one. (A text sort would get 4.10.970.0 vs 4.10.3112.0
    // backwards, which is why the finder ranks by version number.)
    const Widevine::Cdm found = Widevine::find({dir.path()});
    QCOMPARE(found.path, newCdm);
    QCOMPARE(found.version, QStringLiteral("4.10.3112.0"));

    // No CDM anywhere is an empty answer, not an error: the app still has to start
    // and fall back to the browser.
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    QVERIFY(!Widevine::find({empty.path()}).isValid());
    QVERIFY(Widevine::describe({}).contains(QStringLiteral("no Widevine CDM")));

    // A version directory whose module is missing is not usable either — that is
    // exactly what a half-finished component update leaves behind.
    QTemporaryDir broken;
    QVERIFY(broken.isValid());
    QVERIFY(QDir().mkpath(broken.path()
                          + QStringLiteral("/B/WidevineCdm/4.10.9999.0/_platform_specific/linux_x64")));
    QVERIFY(!Widevine::find({broken.path()}).isValid());

    // An explicit path is preferred outright, even where a profile copy exists, and
    // its version is still read off the layout rather than reported as the platform
    // directory ("linux_x64") the module happens to sit in.
    qputenv("HURUWAVE_WIDEVINE", newCdm.toUtf8());
    QCOMPARE(Widevine::find({dir.path()}).path, newCdm);
    QCOMPARE(Widevine::find({dir.path()}).version, QStringLiteral("4.10.3112.0"));
    QCOMPARE(Widevine::find().path, newCdm);  // no roots: the override is the answer
    // A CDM kept without a version directory at all still works, it just has no
    // version to report.
    const QString flat = dir.path() + QStringLiteral("/flat/libwidevinecdm.so");
    QVERIFY(QDir().mkpath(dir.path() + QStringLiteral("/flat")));
    QFile flatFile(flat);
    QVERIFY(flatFile.open(QIODevice::WriteOnly));
    flatFile.write("flat");
    flatFile.close();
    qputenv("HURUWAVE_WIDEVINE", flat.toUtf8());
    QCOMPARE(Widevine::find({empty.path()}).path, flat);
    QVERIFY(!Widevine::find({empty.path()}).version.contains(QStringLiteral(".")));
    // A stale override that points nowhere is ignored, not reported.
    qputenv("HURUWAVE_WIDEVINE", "/nonexistent/libwidevinecdm.so");
    QVERIFY(!Widevine::find({empty.path()}).isValid());
    qunsetenv("HURUWAVE_WIDEVINE");

    if (!savedOverride.isEmpty())
        qputenv("HURUWAVE_WIDEVINE", savedOverride);
}

// Borrowing the session IS the sign-in story for the Apple surface, and it fails
// invisibly (the panel just opens unsigned), so the jar rules are tested: HttpOnly
// cookies, session cookies, values with tabs, an out-of-range expiry, and what makes
// a jar a session rather than Apple's tracking crumbs.
void HuruwaveTests::readsTheAppleSessionJar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("apple-cookies.txt"));

    // The shape yt-dlp writes: comments, other sites, HttpOnly marked by a prefix on
    // the DOMAIN field, a session cookie (expiry 0), a real expiry, a value with a
    // tab in it, and — as the real jar does — an expiry in Chrome's microseconds
    // that no QDateTime can hold.
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("# Netscape HTTP Cookie File\n"
               "# generated by yt-dlp\n"
               "\n"
               "accounts.google.com\tFALSE\t/\tTRUE\t13440635416000000\tOTZ\tFAKEgooglevalue\n"
               ".apple.com\tTRUE\t/\tTRUE\t0\tmyacinfo\tFAKEmyacinfovalue\n"
               "#HttpOnly_.music.apple.com\tTRUE\t/\tTRUE\t1795000000\tmedia-user-token\t0.FAKEmediausertoken\twith-tab\n"
               ".idmsa.apple.com\tTRUE\t/\tTRUE\t0\taasp\tFAKEaaspvalue\n"
               ".apple.com\tTRUE\t/\tFALSE\t13440635416000000\ts_vi\tabc\n"
               "not a cookie line\n");
    file.close();

    const AppleSession::Jar jar = AppleSession::readNetscapeFile(path);
    QVERIFY(jar.source);
    QVERIFY(jar.isValid());
    QCOMPARE(jar.cookies.size(), 4);   // Google's cookie and the junk line are out
    QCOMPARE(jar.skipped, 2);
    QVERIFY(jar.domains.contains(QStringLiteral(".music.apple.com")));
    QVERIFY(!jar.domains.contains(QStringLiteral("accounts.google.com")));

    // Order is the file's, and the flags survive.
    QCOMPARE(QString::fromUtf8(jar.cookies.at(0).name()), QStringLiteral("myacinfo"));
    QVERIFY(jar.cookies.at(0).expirationDate().isNull());   // expiry 0 = session cookie
    QVERIFY(!jar.cookies.at(0).isHttpOnly());
    QVERIFY(jar.cookies.at(1).isHttpOnly());                // the #HttpOnly_ prefix
    QVERIFY(jar.cookies.at(1).isSecure());
    QVERIFY(!jar.cookies.at(1).expirationDate().isNull());   // a real expiry survives

    // A value containing a tab belongs to the value: truncating it would silently
    // corrupt a token.
    QCOMPARE(QString::fromUtf8(jar.cookies.at(1).value()),
             QStringLiteral("0.FAKEmediausertoken\twith-tab"));

    // An expiry no clock can represent degrades to a session cookie rather than
    // becoming an invalid date that never matches.
    QVERIFY(jar.cookies.at(3).expirationDate().isNull());

    // What makes it a session, and what the status line says about it.
    QVERIFY(jar.hasSession);
    QVERIFY(jar.sessionCookies.contains(QStringLiteral("media-user-token")));
    QVERIFY(jar.sessionCookies.contains(QStringLiteral("myacinfo")));
    const QString described = AppleSession::describe(jar);
    QVERIFY2(described.contains(QStringLiteral("session present")), qPrintable(described));
    // It names cookies and domains, never a value.
    QVERIFY(!described.contains(QStringLiteral("FAKEmyacinfo")));

    // Tracking crumbs without a session are reported as such rather than as usable.
    const QString crumbs = dir.filePath(QStringLiteral("crumbs.txt"));
    QFile crumbFile(crumbs);
    QVERIFY(crumbFile.open(QIODevice::WriteOnly | QIODevice::Text));
    crumbFile.write(".apple.com\tTRUE\t/\tFALSE\t0\tgeo\tGB\n");
    crumbFile.close();
    const AppleSession::Jar crumbJar = AppleSession::readNetscapeFile(crumbs);
    QVERIFY(crumbJar.isValid());
    QVERIFY(!crumbJar.hasSession);
    QVERIFY(AppleSession::describe(crumbJar).contains(QStringLiteral("NO session cookie")));

    // A missing file is not an error, just no session: the surface then behaves as
    // it did before the borrowing existed.
    const AppleSession::Jar missing = AppleSession::readNetscapeFile(dir.filePath(QStringLiteral("nope.txt")));
    QVERIFY(!missing.source);
    QVERIFY(!missing.isValid());
    QVERIFY(AppleSession::describe(missing).contains(QStringLiteral("no Apple cookie jar")));
}

QTEST_MAIN(HuruwaveTests)
#include "huruwave_tests.moc"
