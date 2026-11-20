#pragma once

#include <QList>
#include <QNetworkCookie>
#include <QString>
#include <QStringList>

// The Apple session the DRM surface borrows.
//
// Apple's own sign-in does not work in an embedded browser on this Qt (its JS dies
// against the anti-abuse endpoints and the modal spins forever — see
// references/huruwave-apple-drm.md), so the surface does not try to sign in at all:
// it borrows the session the user already has in their own browser, the same way
// this app already borrows their YouTube Music session for search and playlists.
// That is per-user state read at runtime — the app is never shipped with
// credentials, and nothing about a session is written into the package.
//
// The carrier is yt-dlp's Netscape cookie jar (`bin/apple-session` writes one from
// the browser; yt-dlp does the OS-keyring decryption), reduced here to the cookies
// that matter.
namespace AppleSession {

struct Jar {
    QList<QNetworkCookie> cookies;  // in file order, Apple domains only
    QStringList domains;            // distinct host_keys, for the status line
    QStringList sessionCookies;     // the names that make it a real session
    bool source = false;            // was the file readable at all
    bool hasSession = false;        // does it carry media-user-token / myacinfo
    int skipped = 0;                // lines that were not Apple cookies
    bool isValid() const { return source && !cookies.isEmpty(); }
};

// Reads a Netscape-format cookie file (yt-dlp's own output) and keeps the Apple
// cookies. A missing or unreadable file is not an error: the surface then simply
// shows Apple's page unsigned, exactly as before this existed.
Jar readNetscapeFile(const QString &path);

// The file the app looks at, in order: $HURUWAVE_APPLE_COOKIES, the `apple_cookies`
// key in ~/.config/huruwave/config.toml, then the default location
// bin/apple-session writes to.
QString defaultPath();

// One line for --widevine-status. Names what was found; never a cookie value, and
// never a session token.
QString describe(const Jar &jar);

}  // namespace AppleSession
