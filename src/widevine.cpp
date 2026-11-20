#include "widevine.h"

#include <QDir>
#include <QFileInfo>
#include <QVersionNumber>

#include <algorithm>

namespace {

// Where the CDM lives inside a Chromium profile, and how the browser versions it.
const char *kCdmDir = "WidevineCdm";
const char *kModule = "_platform_specific/linux_x64/libwidevinecdm.so";

// The profile roots of the Chromium-family browsers, native packages and their
// Flatpaks alike (both are in use on these boxes). Each root is scanned one level
// deep as well, because the Brave variants keep their profile in a subdirectory:
// BraveSoftware/Brave-Origin/WidevineCdm.
QStringList namedProfiles(const QString &home)
{
    return {
        home + QStringLiteral("/.config/BraveSoftware"),
        home + QStringLiteral("/.config/chromium"),
        home + QStringLiteral("/.config/google-chrome"),
        home + QStringLiteral("/.config/microsoft-edge"),
        home + QStringLiteral("/.var/app/com.brave.Browser/config/BraveSoftware"),
        home + QStringLiteral("/.var/app/org.chromium.Chromium/config/chromium"),
        home + QStringLiteral("/.var/app/com.google.Chrome/config/google-chrome"),
        QStringLiteral("/opt/google/chrome"),
        QStringLiteral("/usr/lib64/chromium"),
        QStringLiteral("/usr/lib/chromium"),
    };
}

// Every <dir>/WidevineCdm under one root, plus the root itself when it is one.
QStringList cdmDirs(const QString &root)
{
    QStringList dirs;
    const QString direct = root + QLatin1Char('/') + QLatin1String(kCdmDir);
    if (QFileInfo(direct).isDir())
        dirs << direct;
    const QStringList children =
        QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &child : children) {
        const QString nested = root + QLatin1Char('/') + child + QLatin1Char('/')
                               + QLatin1String(kCdmDir);
        if (QFileInfo(nested).isDir())
            dirs << nested;
    }
    return dirs;
}

// The newest version directory in one WidevineCdm dir whose module really exists.
// A browser partway through an update, or one whose profile kept the directory
// after the module went, must not be reported as usable.
Widevine::Cdm newestIn(const QString &cdmDir)
{
    QStringList versions = QDir(cdmDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    // A name sort puts 4.10.970.0 after 4.10.3112.0, so rank by version, not text.
    std::sort(versions.begin(), versions.end(), [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a) < QVersionNumber::fromString(b);
    });
    for (int i = versions.size() - 1; i >= 0; --i) {
        const QString path = cdmDir + QLatin1Char('/') + versions.at(i) + QLatin1Char('/')
                             + QLatin1String(kModule);
        if (QFileInfo(path).isFile())
            return {path, versions.at(i), cdmDir};
    }
    return {};
}

// The newer of two candidates; either may be invalid.
Widevine::Cdm newest(const Widevine::Cdm &a, const Widevine::Cdm &b)
{
    if (!a.isValid())
        return b;
    if (!b.isValid())
        return a;
    return QVersionNumber::fromString(a.version) >= QVersionNumber::fromString(b.version) ? a : b;
}

}  // namespace

Widevine::Cdm Widevine::find(const QStringList &roots)
{
    // A path set on purpose wins outright — otherwise a stale profile copy could
    // shadow the one the user actually chose.
    const QString chosen = qEnvironmentVariable("HURUWAVE_WIDEVINE");
    if (!chosen.isEmpty()) {
        const QFileInfo info(chosen);
        if (info.isFile()) {
            // The version is whatever ancestor directory is named like one: in the
            // conventional <WidevineCdm>/<version>/_platform_specific/linux_x64/
            // libwidevinecdm.so layout that is two levels up, not the module's own
            // directory. Without one, the path still works — it just has no version.
            QDir dir = info.dir();
            for (int level = 0; level < 3; ++level) {
                // A name that is not a version parses to a null version number, so
                // this is the whole test — no regex, no false positives on "linux_x64".
                const QVersionNumber parsed = QVersionNumber::fromString(dir.dirName());
                if (!parsed.isNull())
                    return {info.absoluteFilePath(), dir.dirName(), dir.absolutePath()};
                if (!dir.cdUp())
                    break;
            }
            return {info.absoluteFilePath(), info.dir().dirName(), info.dir().absolutePath()};
        }
    }

    const QString home = QDir::homePath();
    const QStringList search = roots.isEmpty() ? namedProfiles(home) : roots;
    Cdm best;
    for (const QString &root : search) {
        const QStringList dirs = cdmDirs(root);
        for (const QString &cdmDir : dirs)
            best = newest(best, newestIn(cdmDir));
    }
    return best;
}

QString Widevine::describe(const Cdm &cdm)
{
    if (cdm.isValid())
        return QStringLiteral("Widevine CDM %1 found at %2").arg(cdm.version, cdm.path);
    return QStringLiteral("no Widevine CDM found — let Brave or Chromium open a DRM page once "
                          "(its component updater fetches one per user), or point "
                          "HURUWAVE_WIDEVINE at a copy");
}
