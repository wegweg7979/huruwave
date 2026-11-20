import QtQuick
import QtQuick.Layouts

// `?` shows every shortcut, the way the rest of the desktop does it. Clicking
// anywhere closes it.
Rectangle {
    id: overlay

    color: "#e6" + theme.darkerBackground.substring(1)

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.visible = false
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 120, 520)
        spacing: 14

        Text {
            text: qsTr("SHORTCUTS")
            color: theme.accent
            font.family: theme.fontFamily
            font.pixelSize: 12
            font.letterSpacing: 3
            font.bold: true
        }

        Repeater {
            model: [
                ["Space", qsTr("play / pause")],
                ["←  →", qsTr("seek 5 seconds")],
                ["Ctrl + ←  →", qsTr("previous / next track")],
                ["Ctrl + L", qsTr("jump to the search box")],
                ["Click a list row", qsTr("play it, and load the list as the queue")],
                ["Drop files", qsTr("queue local files")],
                ["?", qsTr("this list")],
                ["Ctrl + Q", qsTr("quit")]
            ]

            delegate: RowLayout {
                Layout.fillWidth: true
                spacing: 20

                Text {
                    Layout.preferredWidth: 130
                    text: modelData[0]
                    color: theme.brightForeground
                    font.family: theme.fontFamily
                    font.pixelSize: 12
                }

                Text {
                    Layout.fillWidth: true
                    text: modelData[1]
                    color: theme.foreground
                    font.family: theme.fontFamily
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }
}
