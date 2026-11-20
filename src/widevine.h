#pragma once

#include <QString>
#include <QStringList>

// The Widevine CDM an embedded Chromium (QtWebEngine) needs to play DRM audio —
// in this app, Apple Music's full tracks, which Apple never serves as a plain
// stream the way it serves the 30-second previews.
//
// The CDM is proprietary and licensed to a person, not to an application, so it
// is deliberately NOT part of this package and must never be installed
// system-wide by it: a package that shipped `libwidevinecdm.so` would be
// redistributing something nobody here is licensed to distribute. What this
// namespace does instead is FIND the copy the machine already has — every
// Chromium-family browser downloads it into its own profile through Google's
// component updater, which is exactly the per-user acquisition path that is
// allowed. Nothing is copied and nothing is written: the path is handed to
// QtWebEngine, which loads it.
namespace Widevine {

struct Cdm {
    QString path;     // the libwidevinecdm.so to pass to --widevine-path
    QString version;  // e.g. 4.10.3112.0, read off the directory name
    QString origin;   // the directory it came from, for the status line
    bool isValid() const { return !path.isEmpty(); }
};

// `roots` is for tests: pass your own tree. Empty means "look where a browser on
// this machine would have put it" (see namedProfiles()).
//
// Order of preference:
//   1. $HURUWAVE_WIDEVINE — an explicit path, for a CDM kept somewhere else
//      (a Chrome RPM, an extracted tarball, a second machine's copy). It wins
//      even if a profile copy exists, because it was set on purpose.
//   2. The browser profiles, HIGHEST VERSION first: an old CDM that the browser
//      has already superseded must not shadow the current one.
Cdm find(const QStringList &roots = {});

// One line for --widevine-status and for the panel's own explanation. Says what
// was found and, when nothing was, what the user can do about it.
QString describe(const Cdm &cdm);

}  // namespace Widevine
