import QtQuick
import QtQuick.Layouts
import QtWebEngine

// Full-track Apple Music, inside the window.
//
// Apple's catalog API serves a 30-second preview and nothing else; the full track
// is DRM-protected and needs a Widevine CDM, which mpv — the engine everything
// else in this app plays through — has no way to use. So this is the one surface
// in huruwave that is not mpv: a Chromium view carrying the CDM the user's own
// browser already fetched, showing Apple's own web player.
//
// Signing in is Apple's, not ours: Apple's sign-in JS does not survive an embedded
// browser on this Qt (references/huruwave-apple-drm.md), so the surface borrows the
// session the user's own browser already has — the same per-user borrowing the
// YouTube Music side does with yt-dlp cookies.
//
// It is only ever created when C++ says the build can do it (QtWebEngine present
// AND a CDM found), so an ordinary build keeps the external-browser behaviour
// instead of showing a page that cannot play.
Item {
    id: root

    property url targetUrl
    property string heading: ""
    signal closed()

    // The profile is NOT created here: the surface borrows the user's Apple session,
    // and injecting cookies needs QtWebEngine's C++ cookie store, so main.cpp builds
    // the profile (storage name, persistent cookies, UA) and exposes it as the
    // `appleProfile` context property. This view simply uses it.

    Rectangle {
        anchors.fill: parent
        color: theme.darkerBackground
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: what is open, and the two ways out — the same shape as the main
        // window's header, so this reads as a place in the app, not a browser.
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
                    text: qsTr("APPLE MUSIC")
                    color: theme.brightForeground
                    font.family: theme.fontFamily
                    font.pixelSize: 13
                    font.letterSpacing: 3
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: root.heading
                    color: theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Text {
                    text: qsTr("OPEN IN BROWSER")
                    color: theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                    font.letterSpacing: 1
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: Qt.openUrlExternally(view.url) }
                }

                Text {
                    text: qsTr("CLOSE")
                    color: theme.accent
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                    font.letterSpacing: 1
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.closed() }
                }
            }
        }

        // The page itself. Focus so the sign-in form takes the keyboard as soon as
        // this is on screen, rather than needing a click to be typed into.
        WebEngineView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Built in C++, because the borrowed session's cookies go in through a
            // cookie store QML cannot reach (see main.cpp).
            profile: appleProfile
            url: root.targetUrl
            focus: true
            // A sign-in flow finishes in a popup window. Without this the popup is
            // never shown and the page sits on "signing in…" forever.
            onNewWindowRequested: (request) => request.openIn(view)
        }
    }
}
