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
    // True when this build can play a DRM-protected full track in-window (built
    // with QtWebEngine and a Widevine CDM was found on the machine).
    property bool fullTrackAvailable: false
    // True while the Apple DRM surface is up: it fills the window, so this panel —
    // the spectrum bars included — is behind it and there is nothing to animate.
    property bool surfaceOpen: false
    signal fullTrackRequested()
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

            Text {
                width: parent.width
                visible: panel.track && panel.track.album && panel.track.album.length > 0
                text: panel.track && panel.track.album ? panel.track.album : ""
                color: theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        PulseWave {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            // Nothing to animate while the DRM surface covers this panel — and the
            // fallback must not take over in the analyser's place, or the app would
            // keep repainting the window for bars behind it (Main.qml's audioActive).
            playing: panel.playing && !panel.surfaceOpen
            colour: theme.accent
            // Real bands while cava is feeding us frames; empty means the panel
            // falls back to its synthetic animation (see PulseWave.qml).
            values: panel.surfaceOpen ? [] : visualizer.values
        }

        Item { Layout.fillHeight: true }

        // Apple serves a 30-second preview unless the track is owned outright, and
        // saying so stops a 0:30 running time from looking like a bug. When this
        // build can play the real thing, the line offers it (clickable, and on F)
        // instead of pointing at the browser.
        Text {
            Layout.fillWidth: true
            visible: panel.track !== null && panel.track.url
                     && panel.track.url.indexOf("AudioPreview") >= 0
            text: panel.fullTrackAvailable && panel.track.webUrl
                  ? qsTr("Apple Music preview · F plays the full track here")
                  : qsTr("Apple Music preview · the full track opens in your browser")
            color: theme.yellow
            font.family: theme.fontFamily
            font.pixelSize: 10
            wrapMode: Text.WordWrap
            HoverHandler {
                cursorShape: panel.fullTrackAvailable && panel.track && panel.track.webUrl
                             ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
            TapHandler {
                enabled: panel.fullTrackAvailable && panel.track !== null
                         && panel.track.webUrl
                onTapped: panel.fullTrackRequested()
            }
        }

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
