import QtQuick
import QtQuick.Layouts

// The transport: what cliamp puts on its bottom line, as a bar.
//
// State arrives as properties and leaves as signals, because this file cannot
// see Main.qml's ids -- each QML file is its own scope.
Rectangle {
    id: bar

    property bool shuffle: false
    property int repeatMode: 0
    property int queueCount: 0

    signal previousRequested()
    signal nextRequested()
    signal shuffleToggled()
    signal repeatCycled()
    signal queueRequested()
    signal helpRequested()

    color: theme.darkBackground

    function formatTime(seconds) {
        if (!seconds || seconds < 0 || !isFinite(seconds))
            return "0:00"
        const total = Math.floor(seconds)
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const secs = total % 60
        const pad = (value) => (value < 10 ? "0" + value : "" + value)
        return hours > 0
               ? hours + ":" + pad(minutes) + ":" + pad(secs)
               : minutes + ":" + pad(secs)
    }

    // The progress line doubles as the bar's top border: click anywhere on it to
    // seek, the way the Omarchy bar handles its own sliders. It lives INSIDE the
    // bar (not above it) so it can never be covered by the list.
    Item {
        id: progressArea
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 4

        Rectangle {
            anchors.fill: parent
            color: theme.selection
        }

        Rectangle {
            width: parent.width * (player.duration > 0 ? Math.min(1, player.position / player.duration) : 0)
            height: parent.height
            color: theme.accent
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }

        TapHandler {
            onTapped: (event) => {
                if (player.duration > 0)
                    player.seekAbsolute((event.position.x / progressArea.width) * player.duration)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 4
        spacing: 20

        Text {
            Layout.preferredWidth: 96
            Layout.alignment: Qt.AlignVCenter
            text: bar.formatTime(player.position) + " / " + bar.formatTime(player.duration)
            color: theme.muted
            font.family: theme.fontFamily
            font.pixelSize: 11
        }

        Item { Layout.fillWidth: true }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: 24

            Text {
                text: "⏮"
                color: theme.foreground
                font.family: theme.fontFamily
                font.pixelSize: 18
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: bar.previousRequested() }
            }

            Text {
                text: player.playing && !player.paused ? "⏸" : "▶"
                color: theme.accent
                font.family: theme.fontFamily
                font.pixelSize: 22
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: player.togglePause() }
            }

            Text {
                text: "⏭"
                color: theme.foreground
                font.family: theme.fontFamily
                font.pixelSize: 18
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: bar.nextRequested() }
            }
        }

        Item { Layout.fillWidth: true }

        RowLayout {
            spacing: 16

            Text {
                text: bar.shuffle ? "SHUF" : "shuf"
                color: bar.shuffle ? theme.accent : theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 11
                font.letterSpacing: 1
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: bar.shuffleToggled() }
            }

            Text {
                text: ["rep-off", "rep-all", "rep-one"][bar.repeatMode]
                color: bar.repeatMode > 0 ? theme.accent : theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 11
                font.letterSpacing: 1
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: bar.repeatCycled() }
            }

            Text {
                text: "vol " + player.volume
                color: player.volume > 0 ? theme.muted : theme.red
                font.family: theme.fontFamily
                font.pixelSize: 11
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: player.setVolumeFromQml(player.volume > 0 ? 0 : 100) }

                // Scroll to nudge the volume, like a bar widget.
                WheelHandler {
                    onWheel: (event) => {
                        player.setVolumeFromQml(player.volume + (event.angleDelta.y > 0 ? 5 : -5))
                    }
                }
            }

            Text {
                text: "queue " + bar.queueCount
                color: theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 11
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: bar.queueRequested() }
            }

            Text {
                text: "?"
                color: theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 13
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: bar.helpRequested() }
            }
        }
    }
}
