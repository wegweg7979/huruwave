#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

// Follows the live Omarchy theme: every colour the UI needs, plus Hyprland's
// window rounding. Omarchy replaces the theme directory's contents on a theme
// switch (files are rewritten and re-symlinked), so the watcher is re-armed on
// every change rather than trusted to keep firing.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString darkBackground READ darkBackground NOTIFY changed)
    Q_PROPERTY(QString darkerBackground READ darkerBackground NOTIFY changed)
    Q_PROPERTY(QString lighterBackground READ lighterBackground NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString brightForeground READ brightForeground NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString selection READ selection NOTIFY changed)
    Q_PROPERTY(QString red READ red NOTIFY changed)
    Q_PROPERTY(QString yellow READ yellow NOTIFY changed)
    Q_PROPERTY(QString green READ green NOTIFY changed)
    Q_PROPERTY(QString cyan READ cyan NOTIFY changed)
    Q_PROPERTY(QString blue READ blue NOTIFY changed)
    Q_PROPERTY(QString magenta READ magenta NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)

public:
    // Both paths are injectable so the tests need no live desktop.
    explicit Theme(const QString &currentDirectory = {}, const QString &stateDirectory = {},
                   QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString name() const { return m_name; }
    QString accent() const { return m_accent; }
    QString background() const { return m_background; }
    QString darkBackground() const { return m_darkBackground; }
    QString darkerBackground() const { return m_darkerBackground; }
    QString lighterBackground() const { return m_lighterBackground; }
    QString foreground() const { return m_foreground; }
    QString brightForeground() const { return m_brightForeground; }
    QString muted() const { return m_muted; }
    QString selection() const { return m_selection; }
    QString red() const { return m_red; }
    QString yellow() const { return m_yellow; }
    QString green() const { return m_green; }
    QString cyan() const { return m_cyan; }
    QString blue() const { return m_blue; }
    QString magenta() const { return m_magenta; }
    int radius() const { return m_radius; }
    QString fontFamily() const { return m_fontFamily; }

    // Parses Omarchy's colors.toml. Static and path-based for testability.
    static QHash<QString, QString> readColors(const QString &path);

signals:
    void changed();

private:
    void reload();
    void rearmWatcher();
    void refreshRounding();

    QString m_currentDirectory;
    QString m_stateDirectory;
    QString m_name = QStringLiteral("omarchy");
    QString m_accent = QStringLiteral("#7aa2f7");
    QString m_background = QStringLiteral("#1a1b26");
    QString m_darkBackground = QStringLiteral("#13141c");
    QString m_darkerBackground = QStringLiteral("#0e0e14");
    QString m_lighterBackground = QStringLiteral("#24283b");
    QString m_foreground = QStringLiteral("#a9b1d6");
    QString m_brightForeground = QStringLiteral("#c0caf5");
    QString m_muted = QStringLiteral("#414868");
    QString m_selection = QStringLiteral("#292e42");
    QString m_red = QStringLiteral("#f7768e");
    QString m_yellow = QStringLiteral("#e0af68");
    QString m_green = QStringLiteral("#9ece6a");
    QString m_cyan = QStringLiteral("#449dab");
    QString m_blue = QStringLiteral("#7aa2f7");
    QString m_magenta = QStringLiteral("#ad8ee6");
    QString m_fontFamily = QStringLiteral("JetBrainsMono Nerd Font");
    int m_radius = 0;

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll;
};
