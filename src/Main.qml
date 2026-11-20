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
    // Which catalog the search box talks to: "ytmusic" or "apple".
    property string provider: "ytmusic"

    // The in-app Apple Music surface. `appleWebAvailable` comes from C++ and is
    // true only when this build has QtWebEngine AND a Widevine CDM was found on
    // the machine, so a build with neither keeps the external-browser behaviour
    // instead of opening a page that cannot play.
    property bool appleWebOpen: false
    property url appleWebUrl: ""
    property string appleWebHeading: ""

    // The analyser follows whichever engine is making sound. mpv covers YouTube
    // Music and local files; Apple's DRM audio plays in the web surface and never
    // reaches mpv at all, so it has to be counted here — otherwise the bars would
    // go flat exactly when Apple Music starts. A pause keeps the analyser alive on
    // purpose: real silence is an honest flat line.
    readonly property bool audioActive: appleWebOpen || player.playing
    onAudioActiveChanged: visualizer.active = audioActive

    ListModel { id: results }
    ListModel { id: queue }

    // A YouTube id in a URL is enough to build the artwork URL, so a track
    // started from the command line still shows cover art.
    function youtubeId(url) {
        const match = /[?&]v=([A-Za-z0-9_-]{11})/.exec(url)
        return match ? match[1] : ""
    }

    // True for a filename or a URL, and false for a real song title. Needed
    // because mpv's media-title is whatever the container says: for an Apple
    // preview it is the m4a's name, and adopting that would replace "Xtal" with
    // "mzaf_282027520707936830.plus.aac.p.m4a".
    function looksLikePath(text) {
        return text.indexOf("/") >= 0
                || text.indexOf("?") >= 0
                || /\.(m4a|mp3|flac|opus|ogg|wav|webm|aac|m4b|mp4)$/i.test(text)
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

    // Search whichever catalog is selected. Switching provider clears the list
    // rather than leaving one catalog's rows under the other's heading.
    function runSearch(text) {
        window.viewTitle = qsTr("Search")
        results.clear()
        window.statusLine = qsTr("searching…")
        if (window.provider === "apple")
            apple.search(text)
        else
            ytmusic.search(text)
    }

    // Open Apple's own web player in-window for a track the catalog API only
    // offers a preview of. Returns false when this build or machine cannot do it,
    // so the caller can fall back to the browser instead of showing nothing.
    function openAppleWeb(webUrl, heading) {
        if (!appleWebAvailable || !webUrl || webUrl.length === 0)
            return false
        // One stream at a time: leaving the preview playing under the full track
        // would put two copies of the same song in the speakers.
        player.stop()
        appleWebHeading = heading
        appleWebUrl = webUrl
        appleWebOpen = true
        return true
    }

    function closeAppleWeb() {
        appleWebOpen = false
        appleWebUrl = ""
        appleWebHeading = ""
    }

    // The full track for whatever is playing, when it is an Apple preview. Bound
    // to F, and to the preview line in the now-playing panel.
    function openFullTrackForCurrent() {
        if (window.currentTrack === null)
            return
        if (!window.currentTrack.webUrl || window.currentTrack.webUrl.length === 0)
            return
        if (window.openAppleWeb(window.currentTrack.webUrl,
                                window.currentTrack.title + " · " + window.currentTrack.artist))
            window.statusLine = qsTr("full track — press play in the panel")
    }

    // A row with nothing to play (an Apple track with no preview) opens its
    // catalog page rather than pretending to play.
    function activateTrack(list, index) {
        if (index < 0 || index >= list.count)
            return
        const track = list.get(index)
        if (track.url && track.url.length > 0) {
            playTrack(list, index)
            return
        }
        if (track.webUrl && track.webUrl.length > 0) {
            if (window.openAppleWeb(track.webUrl, track.title + " · " + track.artist)) {
                window.statusLine = qsTr("full track — press play in the panel")
                return
            }
            Qt.openUrlExternally(track.webUrl)
            window.statusLine = qsTr("no preview available — opened in your browser")
        }
    }

    // A track that arrived as a bare URL (from the command line, or a desktop-file
    // launch) has only a filename for a title and no artwork. When a catalog
    // search returns the very same URL, take its metadata: the real name, artist,
    // album and cover art. Nothing about playback changes -- the URL is the same.
    function adoptMetadataForCurrent(tracks) {
        if (window.currentTrack === null || !window.looksLikePath(window.currentTrack.title))
            return
        for (let i = 0; i < tracks.length; ++i) {
            const row = tracks[i]
            if (!row.url || row.url !== window.currentTrack.url)
                continue
            const merged = {
                "id": row.id,
                "title": row.title,
                "artist": row.artist,
                "album": row.album !== undefined ? row.album : "",
                "duration": row.duration,
                "thumbnail": row.thumbnail,
                "url": row.url,
                "webUrl": row.webUrl !== undefined ? row.webUrl : "",
                "source": row.source !== undefined ? row.source : "",
            }
            window.currentTrack = merged
            if (window.currentIndex >= 0 && window.currentIndex < queue.count)
                queue.set(window.currentIndex, merged)
            return
        }
    }

    function playTrack(list, index) {
        if (index < 0 || index >= list.count)
            return
        queue.clear()
        let queueIndex = -1
        for (let i = 0; i < list.count; ++i) {
            const row = list.get(i)
            // Rows that cannot play are not queued at all, so the queue never
            // stalls on a track that has no audio behind it.
            if (!row.url || row.url.length === 0)
                continue
            if (i === index)
                queueIndex = queue.count
            queue.append(row)
        }
        if (queueIndex < 0)
            return
        playIndex(queueIndex)
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
            window.adoptMetadataForCurrent(tracks)
        }

        function onFailed(message) {
            statusLine = message
        }
    }

    Connections {
        target: apple

        function onTracksReady(tracks) {
            results.clear()
            for (let i = 0; i < tracks.length; ++i)
                results.append(tracks[i])
            // Say how many can actually be played: Apple serves a 30-second
            // preview for most tracks and nothing at all for the rest, and the
            // difference matters before you click.
            let playable = 0
            for (let i = 0; i < tracks.length; ++i)
                if (tracks[i].url && tracks[i].url.length > 0)
                    ++playable
            window.statusLine = qsTr("%1 songs from Apple Music · %2 with a preview")
                                    .arg(tracks.length).arg(playable)
            window.adoptMetadataForCurrent(tracks)
        }

        function onFailed(message) {
            window.statusLine = message
        }
    }

    Connections {
        target: player

        function onFinished() { window.advance(1) }
        function onLastErrorChanged() { if (player.lastError) window.statusLine = player.lastError }

        // mpv knows the real title once it has resolved the stream, which is how
        // a local file or a bare URL gets a proper name instead of a filename.
        // It is only adopted when the current title IS a filename or URL: a title
        // that came from a provider is better than the container's metadata.
        function onMediaTitleChanged() {
            if (player.mediaTitle.length === 0 || window.currentTrack === null)
                return
            if (window.currentTrack.title === player.mediaTitle)
                return
            if (!window.looksLikePath(window.currentTrack.title))
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
        window.provider = (initialProvider && initialProvider.length > 0) ? initialProvider : "ytmusic"
        if (initialUrl && initialUrl.length > 0) {
            const track = window.trackFromUrl(initialUrl)
            queue.clear()
            queue.append(track)
            window.currentIndex = 0
            window.currentTrack = track
        }
        if (initialPlaylist && initialPlaylist.length > 0) {
            window.viewTitle = initialPlaylist === "LM"
                    ? qsTr("Liked Music")
                    : (initialPlaylist === "WL" ? qsTr("Watch Later") : qsTr("Playlist"))
            ytmusic.loadPlaylist(initialPlaylist)
        }
        if (initialQuery && initialQuery.length > 0) {
            searchField.text = initialQuery
            window.runSearch(initialQuery)
        }
        // --apple-web: open the DRM surface straight away. If this build has no
        // surface (or this machine has no CDM), fall back to the browser rather than
        // doing nothing.
        if (initialAppleWeb && initialAppleWeb.length > 0) {
            if (!window.openAppleWeb(initialAppleWeb, qsTr("opened from the command line")))
                Qt.openUrlExternally(initialAppleWeb)
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

                    // Which catalog the search box is pointed at. Two words, not a
                    // dropdown: there are only ever two, and the accent marks the
                    // live one.
                    Row {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 14

                        Text {
                            text: qsTr("YT MUSIC")
                            color: window.provider === "ytmusic" ? theme.accent : theme.muted
                            font.family: theme.fontFamily
                            font.pixelSize: 11
                            font.letterSpacing: 1
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: window.provider = "ytmusic" }
                        }

                        Text {
                            text: qsTr("APPLE MUSIC")
                            color: window.provider === "apple" ? theme.accent : theme.muted
                            font.family: theme.fontFamily
                            font.pixelSize: 11
                            font.letterSpacing: 1
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: window.provider = "apple" }
                        }
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
                            onAccepted: window.runSearch(text)
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: searchField.verticalCenter
                            visible: searchField.text.length === 0 && !searchField.activeFocus
                            text: window.provider === "apple"
                                  ? qsTr("Search Apple Music")
                                  : qsTr("Search YouTube Music")
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
                    playable: model.url && model.url.length > 0
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
                    playable: model.url && model.url.length > 0
                    selected: ListView.isCurrentItem
                    playing: window.currentTrack !== null
                             && window.currentTrack.url === model.url
                    onActivated: window.activateTrack(results, model.index)
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
            fullTrackAvailable: appleWebAvailable
            onFullTrackRequested: window.openFullTrackForCurrent()
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

    // The DRM surface, created only while it is open: closing it destroys the view
    // (which stops its audio) while its persistent profile keeps the sign-in. It
    // sits above the app and below nothing, so it covers the transport too.
    Loader {
        id: appleWebLoader
        anchors.fill: parent
        active: window.appleWebOpen
        source: "AppleWeb.qml"
        onLoaded: {
            item.targetUrl = window.appleWebUrl
            item.heading = window.appleWebHeading
            item.closed.connect(window.closeAppleWeb)
        }
    }

    // ---- keys ---------------------------------------------------------------
    // Every plain-key shortcut is switched off while the DRM surface is up: the
    // panel is a web page with a sign-in form, and an application-wide Space or F
    // would eat the keystroke the user is typing into Apple's own fields.
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !window.appleWebOpen; onActivated: player.togglePause() }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: !window.appleWebOpen; onActivated: player.seekRelative(5) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: !window.appleWebOpen; onActivated: player.seekRelative(-5) }
    Shortcut { sequence: "Ctrl+Right"; context: Qt.ApplicationShortcut; onActivated: window.advance(1) }
    Shortcut { sequence: "Ctrl+Left"; context: Qt.ApplicationShortcut; onActivated: window.advance(-1) }
    Shortcut { sequence: "Ctrl+L"; context: Qt.ApplicationShortcut; enabled: !window.appleWebOpen; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: help.visible = !help.visible }
    // Escape closes the DRM surface first when it is up, then the help overlay.
    // One handler, not two: two application shortcuts on the same key are
    // ambiguous, and only one of them would ever fire.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (window.appleWebOpen)
                window.closeAppleWeb()
            else
                help.visible = false
        }
    }
    // F: the full track for what is playing (an Apple preview), in-window.
    Shortcut {
        sequence: "F"
        context: Qt.ApplicationShortcut
        enabled: !window.appleWebOpen
        onActivated: window.openFullTrackForCurrent()
    }

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
