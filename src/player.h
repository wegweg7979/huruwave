#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

// Audio playback, driven through mpv's JSON IPC socket.
//
// mpv is the engine on purpose: it resolves YouTube/YouTube Music URLs through
// yt-dlp itself (including the cookies the provider hands it), it handles every
// codec the local library can hold, and it plays gaplessly. Qt Multimedia could
// do none of that without a lot of glue, and doing the decoding here would mean
// linking libav.
//
// Note what this class does NOT own: the queue. The app's queue lives in QML and
// advances on `finished()`, so this stays a transport.
class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit Player(QObject *parent = nullptr, const QString &mpvBinary = {}, const QString &socketPath = {});
    ~Player() override;

    bool playing() const { return m_playing; }
    bool paused() const { return m_paused; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    int volume() const { return m_volume; }
    QString mediaTitle() const { return m_mediaTitle; }
    QString lastError() const { return m_lastError; }

    void setVolume(int volume);
    // Also callable from QML (the wheel handler on the volume readout).
    Q_INVOKABLE void setVolumeFromQml(int volume) { setVolume(volume); }

    // `cookiesFile` / `cookiesFromBrowser` are passed straight to yt-dlp through
    // mpv's raw options; either may be empty.
    void setCookieSource(const QString &cookiesFile, const QString &cookiesFromBrowser);
    Q_INVOKABLE void setCookieSourceQml(const QString &cookiesFile, const QString &cookiesFromBrowser)
    {
        setCookieSource(cookiesFile, cookiesFromBrowser);
    }

    Q_INVOKABLE void play(const QString &url);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekAbsolute(double seconds);
    Q_INVOKABLE void seekRelative(double seconds);

signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mediaTitleChanged();
    void lastErrorChanged();
    // Playback of the current entry ran to its end (reason=eof), which is what
    // advances the queue.
    void finished();
    void started();

private:
    void startEngine();
    void send(const QJsonArray &command, bool withRequestId = true);
    void flushPending();
    void handleLine(const QByteArray &line);
    void handleProperty(int id, const QJsonValue &value);
    void setError(const QString &message);
    void observe(int id, const QString &property);
    void connectSocket();

    QString m_mpv;
    QString m_socketPath;
    QString m_cookiesFile;
    QString m_cookiesFromBrowser;
    QProcess m_process;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    // Commands issued before the socket is up (a file passed on the command line
    // is played immediately, long before mpv has finished starting) are queued
    // here instead of being written into a closed device and lost.
    QList<QByteArray> m_pending;
    QTimer m_reconnect;
    int m_nextRequestId = 100;
    bool m_observed = false;

    bool m_playing = false;
    bool m_paused = false;
    double m_position = 0;
    double m_duration = 0;
    int m_volume = 100;
    QString m_mediaTitle;
    QString m_lastError;
};
