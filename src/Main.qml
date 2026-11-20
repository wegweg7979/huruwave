import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// The app's single window: a sidebar, the current list, now-playing, and the
// transport. Keep layout and keys here; state lives in C++ and in the two models
// this file owns (results and queue).
ApplicationWindow {
    id: window

    width: 1180
    height: 720
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: qsTr("huruwave")
    color: theme.darkerBackground

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: Material.NotRounded

    // The track currently loaded in the engine. The queue holds the entries; this
    // is the one being listened to.
    property var currentTrack: null
    property int currentIndex: -1
    property bool shuffle: false
    property int repeatMode: 0   // 0 = off, 1 = all, 2 = one
    property string viewTitle: qsTr("Search")
    property string statusLine: ""

    ListModel { id: results }
    ListModel { id: queue }

    // A YouTube id in a URL is enough to build the artwork URL, so a track
    // started from the command line still shows cover art.
    function youtubeId(url) {
        const match = /[?&]v=([A-Za-z0-9_-]{11})/.exec(url)
        return match ? match[1] : ""
    }

    function trackFromUrl(url) {
        const id = window.youtubeId(url)
        const name = url.split("/").pop()
        return {
            "title": name,
            "artist": "",
            "duration": "",
            "thumbnail": id ? "https://i.ytimg.com/vi/" + id + "/mqdefault.jpg" : "",
            "url": url
        }
    }

    function playTrack(list, index) {
        if (index < 0 || index >= list.count)
            return
        queue.clear()
        for (let i = 0; i < list.count; ++i)
            queue.append(list.get(i))
        playIndex(index)
    }

    function playIndex(index) {
        if (queue.count === 0)
            return
        currentIndex = index
        currentTrack = queue.get(index)
        player.play(currentTrack.url)
        statusLine = ""
    }

    function advance(step) {
        if (queue.count === 0)
            return
        if (repeatMode === 2) {
            playIndex(currentIndex)
            return
        }
        let next
        if (shuffle) {
            next = Math.floor(Math.random() * queue.count)
            if (queue.count > 1 && next === currentIndex)
                next = (next + 1) % queue.count
        } else {
            next = currentIndex + step
            if (next >= queue.count) {
                if (repeatMode === 1)
                    next = 0
                else {
                    player.stop()
                    return
                }
            }
            if (next < 0)
                next = queue.count - 1
        }
        playIndex(next)
    }

    Connections {
        target: ytmusic

        function onTracksReady(tracks) {
            results.clear()
            for (let i = 0; i < tracks.length; ++i)
                results.append(tracks[i])
            statusLine = tracks.length > 0
                ? qsTr("%1 of %2 results").arg(results.count).arg(results.count)
                : qsTr("nothing found")
        }

        function onFailed(message) {
            statusLine = message
        }
    }

    Connections {
        target: player

        function onFinished() { window.advance(1) }
        function onLastErrorChanged() { if (player.lastError) window.statusLine = player.lastError }

        // mpv knows the real title once it has resolved the stream, which is how
        // a local file or a bare URL gets a proper name instead of a filename.
        function onMediaTitleChanged() {
            if (player.mediaTitle.length === 0 || window.currentTrack === null)
                return
            if (window.currentTrack.title === player.mediaTitle)
                return
            const updated = {
                "title": player.mediaTitle,
                "artist": window.currentTrack.artist,
                "duration": window.currentTrack.duration,
                "thumbnail": window.currentTrack.thumbnail,
                "url": window.currentTrack.url
            }
            window.currentTrack = updated
            if (window.currentIndex >= 0 && window.currentIndex < queue.count)
                queue.set(window.currentIndex, updated)
        }
    }

    // A query from the command line runs as soon as the window exists, so
    // `huruwave --search 'boards of canada'` shows real results instead of an
    // empty list. A URL from the command line is adopted as the current track,
    // because otherwise the panel says "Nothing playing" over audible playback.
    Component.onCompleted: {
        if (initialUrl && initialUrl.length > 0) {
            const track = window.trackFromUrl(initialUrl)
            queue.clear()
            queue.append(track)
            window.currentIndex = 0
            window.currentTrack = track
        }
        if (initialQuery && initialQuery.length > 0) {
            searchField.text = initialQuery
            ytmusic.search(initialQuery)
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: transport.height + 1
        spacing: 0

        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 232

            onSearchRequested: {
                window.viewTitle = qsTr("Search")
                searchField.forceActiveFocus()
                searchField.selectAll()
            }
            onPlaylistRequested: (id, title) => {
                window.viewTitle = title
                ytmusic.loadPlaylist(id)
            }
            onQueueRequested: window.viewTitle = qsTr("Queue")
        }

        // ---- middle column: the list ----------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header: the view's name on the left, the search box on the right.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: theme.darkBackground

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme.selection
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 16

                    Text {
                        text: window.viewTitle.toUpperCase()
                        color: theme.brightForeground
                        font.family: theme.fontFamily
                        font.pixelSize: 13
                        font.letterSpacing: 3
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: window.statusLine
                        color: theme.muted
                        font.family: theme.fontFamily
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    // A flat TextInput, not a Material TextField: Material floats
                    // its placeholder up as a label, which collides with whatever
                    // is beside it and reads as ghosted double text (seen in a
                    // screenshot). This is also closer to how the rest of the
                    // desktop looks: no Material chrome, one accent underline.
                    Item {
                        Layout.preferredWidth: 320
                        Layout.preferredHeight: 28

                        TextInput {
                            id: searchField
                            anchors.fill: parent
                            anchors.bottomMargin: 4
                            color: theme.foreground
                            font.family: theme.fontFamily
                            font.pixelSize: 13
                            selectByMouse: true
                            clip: true
                            verticalAlignment: TextInput.AlignVCenter
                            onAccepted: {
                                window.viewTitle = qsTr("Search")
                                ytmusic.search(text)
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: searchField.verticalCenter
                            visible: searchField.text.length === 0 && !searchField.activeFocus
                            text: qsTr("Search YouTube Music")
                            color: theme.muted
                            font.family: theme.fontFamily
                            font.pixelSize: 13
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: searchField.activeFocus ? theme.accent : theme.selection
                        }
                    }
                }
            }

            // Queue view, when the sidebar asked for it.
            ListView {
                id: queueView
                visible: window.viewTitle === qsTr("Queue")
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: queue
                boundsBehavior: Flickable.StopAtBounds
                delegate: TrackDelegate {
                    width: queueView.width
                    index: model.index
                    title: model.title
                    artist: model.artist
                    duration: model.duration
                    playing: model.index === window.currentIndex
                    onActivated: window.playIndex(model.index)
                }
            }

            ListView {
                id: resultView
                visible: window.viewTitle !== qsTr("Queue")
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: results
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                delegate: TrackDelegate {
                    width: resultView.width
                    index: model.index
                    title: model.title
                    artist: model.artist
                    duration: model.duration
                    selected: ListView.isCurrentItem
                    playing: window.currentTrack !== null
                             && window.currentTrack.url === model.url
                    onActivated: window.playTrack(results, model.index)
                }

                // A list with nothing in it says why, rather than looking broken.
                Text {
                    anchors.centerIn: parent
                    visible: resultView.count === 0
                    width: parent.width - 80
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: ytmusic.busy ? qsTr("working…")
                                       : qsTr("Type a search above, or pick Your Likes in the sidebar.")
                    color: theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 13
                }
            }
        }

        NowPlaying {
            id: nowPlaying
            Layout.fillHeight: true
            Layout.preferredWidth: 344
            track: window.currentTrack
            playing: player.playing && !player.paused
            position: player.position
            duration: player.duration
            cookiedFrom: ytmusic.cookieSource
        }
    }

    TransportBar {
        id: transport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 64

        shuffle: window.shuffle
        repeatMode: window.repeatMode
        queueCount: queue.count

        onPreviousRequested: window.advance(-1)
        onNextRequested: window.advance(1)
        onShuffleToggled: window.shuffle = !window.shuffle
        onRepeatCycled: window.repeatMode = (window.repeatMode + 1) % 3
        onQueueRequested: window.viewTitle = qsTr("Queue")
        onHelpRequested: help.visible = !help.visible
    }

    HelpOverlay {
        id: help
        anchors.fill: parent
        visible: false
    }

    // ---- keys ---------------------------------------------------------------
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: player.togglePause() }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; onActivated: player.seekRelative(5) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; onActivated: player.seekRelative(-5) }
    Shortcut { sequence: "Ctrl+Right"; context: Qt.ApplicationShortcut; onActivated: window.advance(1) }
    Shortcut { sequence: "Ctrl+Left"; context: Qt.ApplicationShortcut; onActivated: window.advance(-1) }
    Shortcut { sequence: "Ctrl+L"; context: Qt.ApplicationShortcut; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: help.visible = !help.visible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: help.visible = false }

    // Local files dropped on the window are queued, which is the fastest way to
    // get a folder of music in without a file dialog.
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            for (let i = 0; i < drop.urls.length; ++i) {
                const url = drop.urls[i]
                queue.append({
                    "title": url.toString().split("/").pop(),
                    "artist": "",
                    "duration": "",
                    "thumbnail": "",
                    "url": url.toString()
                })
            }
            window.viewTitle = qsTr("Queue")
            if (window.currentIndex < 0)
                window.playIndex(0)
        }
    }
}
