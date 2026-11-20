import QtQuick

// One row in a list. Uses Item as its root, never ItemView, and centres its
// content itself: a ListView pins delegates to x=0.
Item {
    id: row

    property int index: -1
    property string title: ""
    property string artist: ""
    property string duration: ""
    property bool playing: false
    property bool selected: false
    // False for a catalog row with nothing to play (an Apple track with no
    // preview): the row still shows, but it offers its web page instead of a
    // play triangle, so the UI never promises audio it cannot deliver.
    property bool playable: true

    signal activated()

    height: 46

    Rectangle {
        anchors.fill: parent
        color: row.playing ? "#26" + theme.accent.substring(1)
                           : (hover.hovered || row.selected ? theme.selection : "transparent")

        // The playing row gets the accent bar too, so the current track is
        // findable at a glance in a long list.
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height - 12
            color: row.playing ? theme.accent : "transparent"
        }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        Text {
            width: 22
            text: row.playing ? "▶" : (row.playable ? (row.index + 1) : "↗")
            color: row.playing ? theme.accent : theme.muted
            font.family: theme.fontFamily
            font.pixelSize: 11
            horizontalAlignment: Text.AlignRight
        }

        Column {
            width: row.width - 20 - 20 - 22 - 14 - durationText.width - 14 - theme.radius
            spacing: 2

            Text {
                width: parent.width
                text: row.title
                color: row.playing ? theme.accent : theme.foreground
                font.family: theme.fontFamily
                font.pixelSize: 13
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: row.artist.length > 0
                text: row.artist
                color: theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        Text {
            id: durationText
            text: row.duration === "NA" ? "" : row.duration
            color: theme.muted
            font.family: theme.fontFamily
            font.pixelSize: 11
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: row.activated() }
}
