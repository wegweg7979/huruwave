import QtQuick
import QtQuick.Layouts

// The left rail: what to browse. Deliberately words, not icons -- Omarchy's own
// apps label things and keep the typography doing the work.
Rectangle {
    id: sidebar

    signal searchRequested()
    signal playlistRequested(string id, string title)
    signal queueRequested()

    color: theme.darkBackground

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: theme.selection
    }

    property int activeIndex: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // Wordmark. The dot is the one piece of motion in the rail: it lights up
        // while something is playing.
        RowLayout {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 22
            Layout.bottomMargin: 26
            spacing: 10

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: player.playing ? theme.accent : theme.muted
                Behavior on color { ColorAnimation { duration: 200 } }
            }

            Text {
                text: "HURUWAVE"
                color: theme.brightForeground
                font.family: theme.fontFamily
                font.pixelSize: 14
                font.letterSpacing: 4
                font.bold: true
            }
        }

        SidebarItem {
            Layout.fillWidth: true
            label: qsTr("SEARCH")
            active: sidebar.activeIndex === 0
            onActivated: { sidebar.activeIndex = 0; sidebar.searchRequested() }
        }
        SidebarItem {
            Layout.fillWidth: true
            label: qsTr("YOUR LIKES")
            active: sidebar.activeIndex === 1
            onActivated: {
                sidebar.activeIndex = 1
                sidebar.playlistRequested("LM", qsTr("Your Likes"))
            }
        }
        SidebarItem {
            Layout.fillWidth: true
            label: qsTr("WATCH LATER")
            active: sidebar.activeIndex === 2
            onActivated: {
                sidebar.activeIndex = 2
                sidebar.playlistRequested("WL", qsTr("Watch Later"))
            }
        }
        SidebarItem {
            Layout.fillWidth: true
            label: qsTr("QUEUE")
            active: sidebar.activeIndex === 3
            onActivated: { sidebar.activeIndex = 3; sidebar.queueRequested() }
        }

        Item { Layout.fillHeight: true }

        // Where the account comes from. If the cookie source is empty the app is
        // browsing anonymously, and saying so is better than showing an empty
        // playlist with no explanation.
        Text {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 18
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            // The truth about the cookie source, worked out by reading it. A
            // cookies file that exists is not a signed-in session: this machine's
            // export held only visitor cookies, every `SID` dropped, so saying
            // "signed in" here is what made a broken account look fine.
            text: ytmusic.cookieStatus
            color: ytmusic.cookieStatus.indexOf(qsTr("signed in")) === 0 ? theme.muted : theme.yellow
            font.family: theme.fontFamily
            font.pixelSize: 10
            lineHeight: 1.35
        }
    }

    // Inline component: one rail row. Written here because the rail is the only
    // place that uses it.
    component SidebarItem: Item {
        id: item
        implicitHeight: 38

        property string label: ""
        property bool active: false
        signal activated()

        Rectangle {
            anchors.fill: parent
            color: hover.hovered ? theme.selection : "transparent"

            // The active row is marked by a 2px accent bar, the way Omarchy marks
            // the focused item, rather than by a filled block.
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 2
                height: parent.height - 12
                color: item.active ? theme.accent : "transparent"
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            text: item.label
            color: item.active ? theme.accent : theme.foreground
            font.family: theme.fontFamily
            font.pixelSize: 12
            font.letterSpacing: 2
        }

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: item.activated() }
    }
}
