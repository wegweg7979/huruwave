#include "player.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStandardPaths>

namespace {

// Observed property ids. mpv answers `observe_property` with the id we chose, so
// these numbers are the wire protocol between us and the engine.
enum PropertyId {
    IdTimePos = 1,
    IdDuration = 2,
    IdPause = 3,
    IdVolume = 4,
    IdMediaTitle = 5,
    IdIdleActive = 6,
};

}  // namespace

Player::Player(QObject *parent, const QString &mpvBinary, const QString &socketPath)
    : QObject(parent),
      m_mpv(mpvBinary.isEmpty() ? QStringLiteral("mpv") : mpvBinary),
      m_socketPath(socketPath.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                             + QStringLiteral("/huruwave-mpv.sock")
                       : socketPath)
{
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_buffer.clear();
        m_observed = false;
        observe(IdTimePos, QStringLiteral("time-pos"));
        observe(IdDuration, QStringLiteral("duration"));
        observe(IdPause, QStringLiteral("pause"));
        observe(IdVolume, QStringLiteral("volume"));
        observe(IdMediaTitle, QStringLiteral("media-title"));
        observe(IdIdleActive, QStringLiteral("idle-active"));
        m_observed = true;
        // Anything the user asked for before mpv was listening goes now.
        flushPending();
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] {
        m_buffer.append(m_socket.readAll());
        int newline;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            if (!line.trimmed().isEmpty())
                handleLine(line);
        }
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        m_playing = false;
        m_paused = false;
        emit stateChanged();
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (error == QLocalSocket::ServerNotFoundError || error == QLocalSocket::ConnectionRefusedError) {
            // The socket is not up yet (or mpv just restarted); retry quietly.
            m_reconnect.start(300);
            return;
        }
        setError(QStringLiteral("mpv socket: %1").arg(m_socket.errorString()));
    });

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &Player::connectSocket);

    startEngine();
}

Player::~Player()
{
    if (m_process.state() != QProcess::NotRunning) {
        send({QStringLiteral("quit")});
        m_process.waitForFinished(500);
        if (m_process.state() != QProcess::NotRunning)
            m_process.kill();
    }
}

void Player::setError(const QString &message)
{
    if (message == m_lastError)
        return;
    m_lastError = message;
    emit lastErrorChanged();
}

void Player::setCookieSource(const QString &cookiesFile, const QString &cookiesFromBrowser)
{
    const bool changed = cookiesFile != m_cookiesFile || cookiesFromBrowser != m_cookiesFromBrowser;
    m_cookiesFile = cookiesFile;
    m_cookiesFromBrowser = cookiesFromBrowser;
    // The cookie source is a command-line option, so it takes effect on the next
    // engine start rather than mid-flight.
    if (changed && m_process.state() != QProcess::NotRunning) {
        m_process.closeWriteChannel();
        m_process.terminate();
        m_process.waitForFinished(1500);
        startEngine();
    }
}

void Player::startEngine()
{
    if (m_process.state() != QProcess::NotRunning)
        return;

    QFile::remove(m_socketPath);
    QDir().mkpath(QFileInfo(m_socketPath).absolutePath());

    QStringList args{
        QStringLiteral("--idle=yes"),
        QStringLiteral("--no-video"),
        QStringLiteral("--no-terminal"),
        QStringLiteral("--really-quiet"),
        QStringLiteral("--keep-open=no"),
        QStringLiteral("--gapless-audio=yes"),
        QStringLiteral("--audio-display=no"),
        QStringLiteral("--input-ipc-server=%1").arg(m_socketPath),
        QStringLiteral("--volume=%1").arg(m_volume),
    };

    // yt-dlp options are split by commas, so a value containing one cannot be
    // passed -- which is fine: paths and browser specs never contain commas.
    QStringList ytdl;
    if (!m_cookiesFile.isEmpty())
        ytdl << QStringLiteral("cookies=%1").arg(m_cookiesFile);
    if (!m_cookiesFromBrowser.isEmpty())
        ytdl << QStringLiteral("cookies-from-browser=%1").arg(m_cookiesFromBrowser);
    ytdl << QStringLiteral("js-runtimes=node");
    if (!ytdl.isEmpty())
        args << QStringLiteral("--ytdl-raw-options=%1").arg(ytdl.join(QLatin1Char(',')));

    m_process.start(m_mpv, args);
    if (!m_process.waitForStarted(3000)) {
        setError(tr("could not start mpv (%1); is it installed?").arg(m_mpv));
        return;
    }
    m_reconnect.start(100);
}

void Player::connectSocket()
{
    if (m_socket.state() == QLocalSocket::ConnectedState)
        return;
    m_socket.connectToServer(m_socketPath);
}

void Player::observe(int id, const QString &property)
{
    send({QStringLiteral("observe_property"), id, property});
}

void Player::send(const QJsonArray &command, bool withRequestId)
{
    QJsonObject request;
    request.insert(QStringLiteral("command"), command);
    if (withRequestId)
        request.insert(QStringLiteral("request_id"), m_nextRequestId++);
    QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact);
    line.append('\n');

    if (m_socket.state() != QLocalSocket::ConnectedState) {
        // Keeping the order matters: loadfile must not overtake set_property.
        m_pending.append(line);
        connectSocket();
        return;
    }
    m_socket.write(line);
    m_socket.flush();
}

void Player::flushPending()
{
    if (m_pending.isEmpty())
        return;
    for (const QByteArray &line : m_pending)
        m_socket.write(line);
    m_socket.flush();
    m_pending.clear();
}

void Player::play(const QString &url)
{
    if (url.isEmpty())
        return;
    m_position = 0;
    m_duration = 0;
    emit positionChanged();
    emit durationChanged();
    // `replace` so the new entry starts immediately instead of being appended to
    // an idle playlist; the app's own queue is the source of order.
    send({QStringLiteral("loadfile"), url, QStringLiteral("replace")});
    send({QStringLiteral("set_property"), QStringLiteral("pause"), false});
}

void Player::togglePause()
{
    send({QStringLiteral("cycle"), QStringLiteral("pause")});
}

void Player::pause()
{
    send({QStringLiteral("set_property"), QStringLiteral("pause"), true});
}

void Player::resume()
{
    send({QStringLiteral("set_property"), QStringLiteral("pause"), false});
}

void Player::stop()
{
    send({QStringLiteral("stop")});
    m_position = 0;
    m_playing = false;
    m_paused = false;
    emit positionChanged();
    emit stateChanged();
}

void Player::seekAbsolute(double seconds)
{
    send({QStringLiteral("seek"), seconds, QStringLiteral("absolute")});
}

void Player::seekRelative(double seconds)
{
    send({QStringLiteral("seek"), seconds, QStringLiteral("relative")});
}

void Player::setVolume(int volume)
{
    volume = qBound(0, volume, 130);
    if (volume == m_volume)
        return;
    m_volume = volume;
    send({QStringLiteral("set_property"), QStringLiteral("volume"), m_volume});
    emit volumeChanged();
}

void Player::handleProperty(int id, const QJsonValue &value)
{
    switch (id) {
    case IdTimePos:
        if (value.isDouble()) {
            m_position = value.toDouble();
            emit positionChanged();
        }
        break;
    case IdDuration:
        if (value.isDouble()) {
            m_duration = value.toDouble();
            emit durationChanged();
        }
        break;
    case IdPause:
        if (value.isBool() && value.toBool() != m_paused) {
            m_paused = value.toBool();
            emit stateChanged();
        }
        break;
    case IdVolume:
        if (value.isDouble()) {
            const int volume = static_cast<int>(value.toDouble());
            if (volume != m_volume) {
                m_volume = volume;
                emit volumeChanged();
            }
        }
        break;
    case IdMediaTitle:
        if (value.isString() && value.toString() != m_mediaTitle) {
            m_mediaTitle = value.toString();
            emit mediaTitleChanged();
        }
        break;
    case IdIdleActive:
        if (value.isBool()) {
            const bool playing = !value.toBool();
            if (playing != m_playing) {
                m_playing = playing;
                emit stateChanged();
                if (playing)
                    emit started();
            }
        }
        break;
    default:
        break;
    }
}

void Player::handleLine(const QByteArray &line)
{
    const QJsonObject event = QJsonDocument::fromJson(line).object();
    const QString name = event.value(QStringLiteral("event")).toString();

    if (name == QStringLiteral("property-change")) {
        const QString property = event.value(QStringLiteral("name")).toString();
        const QJsonValue value = event.value(QStringLiteral("data"));
        // Map the property name back to the id we registered, so a reply to a
        // different observer ordering cannot land in the wrong slot.
        if (property == QStringLiteral("time-pos"))
            handleProperty(IdTimePos, value);
        else if (property == QStringLiteral("duration"))
            handleProperty(IdDuration, value);
        else if (property == QStringLiteral("pause"))
            handleProperty(IdPause, value);
        else if (property == QStringLiteral("volume"))
            handleProperty(IdVolume, value);
        else if (property == QStringLiteral("media-title"))
            handleProperty(IdMediaTitle, value);
        else if (property == QStringLiteral("idle-active"))
            handleProperty(IdIdleActive, value);
        return;
    }

    if (name == QStringLiteral("end-file")) {
        const QString reason = event.value(QStringLiteral("reason")).toString();
        if (reason == QStringLiteral("eof")) {
            emit finished();
        } else if (reason == QStringLiteral("error")) {
            // Say what went wrong rather than looking like a finished track.
            setError(tr("mpv could not play that entry"));
            m_playing = false;
            emit stateChanged();
        }
        return;
    }

    if (name.isEmpty()) {
        // A reply to one of our commands.
        const QJsonValue error = event.value(QStringLiteral("error"));
        if (error.isString() && error.toString() != QStringLiteral("success"))
            setError(QStringLiteral("mpv: %1").arg(error.toString()));
    }
}
