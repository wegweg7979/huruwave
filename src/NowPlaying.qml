import QtQuick
import QtQuick.Layouts

// The right-hand panel: what is playing, and what it looks like.
Rectangle {
    id: panel

    property var track: null
    property bool playing: false
    property double position: 0
    property double duration: 0
    property string cookiedFrom: ""

    color: theme.darkBackground

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: theme.selection
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18

        // Cover art. Sharp corners, and a hairline frame so that a missing
        // thumbnail still looks deliberate rather than empty.
        Rectangle {
            Layout.preferredWidth: 296
            Layout.preferredHeight: 296
            color: theme.darkerBackground
            border.width: 1
            border.color: theme.selection

            Image {
                id: art
                anchors.fill: parent
                anchors.margins: 1
                source: panel.track && panel.track.thumbnail ? panel.track.thumbnail : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: status === Image.Ready
            }

            Text {
                anchors.centerIn: parent
                visible: !art.visible
                text: panel.track ? "♪" : "—"
                color: theme.muted
                font.family: theme.fontFamily
                font.pixelSize: panel.track ? 48 : 28
            }
        }

        Column {
            Layout.fillWidth: true
            spacing: 6

            Text {
                width: parent.width
                text: panel.track ? panel.track.title : qsTr("Nothing playing")
                color: panel.track ? theme.brightForeground : theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 16
                font.bold: true
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: panel.track && panel.track.artist
                text: panel.track ? panel.track.artist : ""
                color: theme.accent
                font.family: theme.fontFamily
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        PulseWave {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            playing: panel.playing
            colour: theme.accent
        }

        Item { Layout.fillHeight: true }

        // Provenance: which account and which app the queue came from.
        Text {
            Layout.fillWidth: true
            text: qsTr("huruwave · %1").arg(panel.playing ? qsTr("playing") : qsTr("idle"))
            color: theme.muted
            font.family: theme.fontFamily
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
