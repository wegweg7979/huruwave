#include "applesession.h"

#include "config.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {

// The cookies that make a jar a SESSION rather than just Apple's tracking crumbs:
// the media user token is what the web player plays with, myacinfo is the Apple ID
// session behind it, and aasp is the idmsa session cookie.
const char *kSessionNames[] = {"media-user-token", "myacinfo", "aasp"};

bool isAppleHost(const QString &host)
{
    // Covers .apple.com and every subdomain the player uses (music, apps, tv,
    // idmsa, podcasts…). A leading dot is part of the Netscape format, not the host.
    QString bare = host;
    while (bare.startsWith(QLatin1Char('.')))
        bare.remove(0, 1);
    return bare == QLatin1String("apple.com") || bare.endsWith(QLatin1String(".apple.com"));
}

}  // namespace

AppleSession::Jar AppleSession::readNetscapeFile(const QString &path)
{
    Jar jar;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return jar;
    jar.source = true;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        // `#HttpOnly_` marks an HttpOnly cookie; every other # is a real comment.
        bool httpOnly = false;
        if (line.startsWith(QLatin1String("#HttpOnly_"))) {
            httpOnly = true;
            line.remove(0, QStringLiteral("#HttpOnly_").size());
        } else if (line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.trimmed().isEmpty())
            continue;

        // domain \t includeSubdomains \t path \t secure \t expires \t name \t value
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 7) {
            ++jar.skipped;
            continue;
        }
        const QString host = fields.at(0).trimmed();
        if (host.isEmpty() || !isAppleHost(host)) {
            ++jar.skipped;
            continue;
        }

        QNetworkCookie cookie;
        cookie.setDomain(host);
        cookie.setPath(fields.at(2).trimmed());
        cookie.setSecure(fields.at(3).trimmed().compare(QLatin1String("TRUE"), Qt::CaseInsensitive) == 0);
        cookie.setHttpOnly(httpOnly);
        // An expiry of 0 is the format's session cookie; keep it one. Some jars carry
        // Chrome's microsecond timestamps in this field, which no QDateTime can hold —
        // fromSecsSinceEpoch then yields an invalid date, setExpirationDate turns that
        // into "no expiry", and the cookie survives as a session cookie rather than
        // disappearing.
        const qint64 expires = fields.at(4).trimmed().toLongLong();
        if (expires > 0)
            cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(expires));
        cookie.setName(fields.at(5).trimmed().toUtf8());
        // Everything after the name is the value: a value may itself contain a tab,
        // and dropping the tail would silently corrupt a token.
        QString value = fields.at(6);
        for (int i = 7; i < fields.size(); ++i)
            value += QLatin1Char('\t') + fields.at(i);
        cookie.setValue(value.toUtf8());
        if (cookie.name().isEmpty())
            continue;

        jar.cookies.append(cookie);
        if (!jar.domains.contains(host))
            jar.domains.append(host);
        const QString name = QString::fromUtf8(cookie.name());
        for (const char *sessionName : kSessionNames) {
            if (name != QLatin1String(sessionName))
                continue;
            jar.hasSession = true;
            if (!jar.sessionCookies.contains(name))
                jar.sessionCookies.append(name);
        }
    }
    jar.domains.sort();
    return jar;
}

QString AppleSession::defaultPath()
{
    const QString fromEnv = qEnvironmentVariable("HURUWAVE_APPLE_COOKIES");
    if (!fromEnv.isEmpty())
        return fromEnv;
    const QString configured = Config::read(QStringLiteral("apple_cookies"));
    if (!configured.isEmpty())
        return configured;
    return QDir::homePath() + QStringLiteral("/.local/share/huruwave/apple-cookies.txt");
}

QString AppleSession::describe(const Jar &jar)
{
    if (!jar.source)
        return QStringLiteral("no Apple cookie jar found — sign in to Apple Music in your browser, "
                              "then run bin/apple-session");
    if (jar.cookies.isEmpty())
        return QStringLiteral("the Apple cookie file holds no Apple cookies (re-run bin/apple-session)");
    QString line = QStringLiteral("%1 Apple cookies from %2")
                       .arg(jar.cookies.size())
                       .arg(jar.domains.join(QStringLiteral(", ")));
    if (jar.hasSession)
        line += QStringLiteral(" — session present (%1)").arg(jar.sessionCookies.join(QStringLiteral(", ")));
    else
        line += QStringLiteral(" — NO session cookie (media-user-token/myacinfo): the surface will "
                               "show Apple's page unsigned");
    return line;
}
