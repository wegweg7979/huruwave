#include "config.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace Config {

QString path()
{
    return QDir::homePath() + QStringLiteral("/.config/huruwave/config.toml");
}

QString read(const QString &key)
{
    return readFrom(path(), key);
}

QString readFrom(const QString &filePath, const QString &key)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    // `key = value`, optionally quoted, optionally followed by a comment. A value
    // that needs a `#` inside it must be quoted, which is how a cookies file path
    // containing a hash survives.
    const QRegularExpression re(QStringLiteral(R"re(^\s*%1\s*=\s*(.*?)\s*(?:#.*)?$)re")
                                    .arg(QRegularExpression::escape(key)));
    while (!file.atEnd()) {
        const auto match = re.match(QString::fromUtf8(file.readLine()));
        if (!match.hasMatch())
            continue;
        QString value = match.captured(1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        return value;
    }
    return {};
}

}  // namespace Config
