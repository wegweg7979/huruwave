#include <QtTest>

#include "theme.h"
#include "visualizer.h"
#include "ytmusic.h"

// The things tested here are the ones that fail silently in production: the
// Omarchy theme parser (a wrong colour is invisible), the YouTube Music search
// parser (a wrong field just shows an empty row), and the cava frame parser (a
// format change would look like the visualiser dying).
class HuruwaveTests : public QObject {
    Q_OBJECT

private slots:
    void parsesOmarchyColors();
    void survivesAMissingTheme();
    void readsQuotedAndBareValues();
    void parsesYtMusicSearchPayload();
    void parsesCavaFrames();
    void dropsGarbledCavaFrames();
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

QTEST_MAIN(HuruwaveTests)
#include "huruwave_tests.moc"
