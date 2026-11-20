#include "theme.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// Omarchy's colors.toml is a flat list of `key = "#rrggbb"`, so a line parser is
// enough and avoids a TOML dependency. The custom delimiter keeps the embedded
// double quotes legal inside the raw literal.
const QRegularExpression &colorLine()
{
    static const QRegularExpression re(
        QStringLiteral(R"re(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)"\s*$)re"));
    return re;
}

QString valueFor(const QHash<QString, QString> &colors, const QString &key, const QString &fallback)
{
    const QString value = colors.value(key);
    return value.isEmpty() ? fallback : value;
}

}  // namespace

Theme::Theme(const QString &currentDirectory, const QString &stateDirectory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_currentDirectory(currentDirectory),
      m_stateDirectory(stateDirectory),
      m_hyprctl(hyprctl)
{
    if (m_currentDirectory.isEmpty()) {
        m_stateDirectory = m_stateDirectory.isEmpty()
            ? QDir::homePath() + QStringLiteral("/.local/state/omarchy")
            : m_stateDirectory;
        m_currentDirectory = m_stateDirectory + QStringLiteral("/current/theme");
    } else if (m_stateDirectory.isEmpty()) {
        m_stateDirectory = QFileInfo(m_currentDirectory).absolutePath() + QStringLiteral("/..");
    }
    if (m_hyprctl.isEmpty())
        m_hyprctl = QStringLiteral("hyprctl");

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Hyprland's rounding can change at runtime (a theme may set it), and it is
    // read through `hyprctl`, so poll it rather than watch a file.
    m_roundingPoll.setInterval(5000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    m_roundingPoll.start();

    reload();
    refreshRounding();
}

Theme::~Theme() = default;

QHash<QString, QString> Theme::readColors(const QString &path)
{
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return colors;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto match = colorLine().match(stream.readLine());
        if (match.hasMatch())
            colors.insert(match.captured(1), match.captured(2));
    }
    return colors;
}

void Theme::reload()
{
    const QString colorsPath = m_currentDirectory + QStringLiteral("/colors.toml");
    const QHash<QString, QString> colors = readColors(colorsPath);

    m_accent = valueFor(colors, QStringLiteral("accent"), m_accent);
    m_background = valueFor(colors, QStringLiteral("background"), m_background);
    m_darkBackground = valueFor(colors, QStringLiteral("dark_background"), m_darkBackground);
    m_darkerBackground = valueFor(colors, QStringLiteral("darker_background"), m_darkerBackground);
    m_lighterBackground = valueFor(colors, QStringLiteral("lighter_background"), m_lighterBackground);
    m_foreground = valueFor(colors, QStringLiteral("foreground"), m_foreground);
    m_brightForeground = valueFor(colors, QStringLiteral("bright_foreground"), m_brightForeground);
    m_muted = valueFor(colors, QStringLiteral("muted"), m_muted);
    m_selection = valueFor(colors, QStringLiteral("selection"), m_selection);
    m_red = valueFor(colors, QStringLiteral("red"), m_red);
    m_yellow = valueFor(colors, QStringLiteral("yellow"), m_yellow);
    m_green = valueFor(colors, QStringLiteral("green"), m_green);
    m_cyan = valueFor(colors, QStringLiteral("cyan"), m_cyan);
    m_blue = valueFor(colors, QStringLiteral("blue"), m_blue);
    m_magenta = valueFor(colors, QStringLiteral("magenta"), m_magenta);

    // The theme name is the directory Omarchy's `current` symlink points at.
    const QFileInfo current(m_stateDirectory + QStringLiteral("/current"));
    const QString target = current.isSymLink() ? current.symLinkTarget() : QString();
    if (!target.isEmpty())
        m_name = QFileInfo(target).fileName();

    rearmWatcher();
    emit changed();
}

void Theme::rearmWatcher()
{
    // A theme switch replaces the watched files, and QFileSystemWatcher drops
    // paths that are removed or replaced, so re-add the directory every reload.
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (QFileInfo::exists(m_currentDirectory))
        m_watcher.addPath(m_currentDirectory);
    const QString colorsPath = m_currentDirectory + QStringLiteral("/colors.toml");
    if (QFileInfo::exists(colorsPath))
        m_watcher.addPath(colorsPath);
    const QString currentLink = m_stateDirectory + QStringLiteral("/current");
    if (QFileInfo::exists(currentLink))
        m_watcher.addPath(currentLink);
}

void Theme::refreshRounding()
{
    if (m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {QStringLiteral("-j"), QStringLiteral("getoption"), QStringLiteral("decoration:rounding")});
    if (!m_roundingProcess.waitForFinished(1200)) {
        m_roundingProcess.kill();
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object();
    if (!root.contains(QStringLiteral("int")))
        return;
    const int radius = root.value(QStringLiteral("int")).toInt();
    if (radius != m_radius) {
        m_radius = radius;
        emit changed();
    }
}
