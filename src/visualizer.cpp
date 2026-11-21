#include "visualizer.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

namespace {

// The panel's design, not a taste knob: the fallback animation draws this many
// bars, so the visual weight does not change when real data arrives (visualizer.cpp
// asks cava for the same count).
constexpr int kBars = 16;
// Deliberately lower than the first design's 30: every analyser frame is a Qt Quick
// repaint of the whole window, and the bars are the app's own continuous render
// load, so halving the rate is the cheapest GPU saving that stays a spectrum. The
// synthetic fallback in PulseWave.qml animates at the same rate.
constexpr int kFrameRate = 15;
// cava's ascii range; the values it prints are 0..this.
constexpr int kAsciiMaxRange = 100;

}  // namespace

Visualizer::Visualizer(QObject *parent, const QString &binary, const QString &configPath)
    : QObject(parent),
      m_binary(binary.isEmpty() ? QStringLiteral("cava") : binary),
      m_configPath(configPath.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                             + QStringLiteral("/huruwave/cava.conf")
                       : configPath)
{
    // An explicit override makes the fallback path testable: with a binary that
    // does not exist, the window must keep animating synthetically.
    const QByteArray override = qgetenv("HURUWAVE_CAVA");
    if (!override.isEmpty())
        m_binary = QString::fromLocal8Bit(override);

    m_process.setProgram(m_binary);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);

    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Visualizer::handleData);

    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // FailedToStart is the "cava is not installed" case, which is expected
        // on a box where it has not been added yet -- say so quietly and let the
        // window fall back, rather than treating it as a crash.
        if (error == QProcess::FailedToStart) {
            noteUnavailable(tr("cava is not installed; using the synthetic animation"));
            return;
        }
        // A Crashed report also arrives for the SIGTERM this class sends on
        // pause, so only a crash while we WANT cava running is a real failure;
        // logging the deliberate stop as "crashed" made a working analyser look
        // broken every time the track was paused.
        if (!m_active)
            return;
        noteUnavailable(tr("cava died while running: %1").arg(m_process.errorString()));
    });

    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                if (!m_active)
                    return;
                // cava exiting on its own while we want it means it could not do
                // its job (unreadable config, no such source). Its stderr is the
                // only place that says why, so keep it in the log.
                const QString reason = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
                noteUnavailable(tr("cava exited (%1) %2").arg(code).arg(reason));
            });
}

Visualizer::~Visualizer()
{
    stop();
}

void Visualizer::noteUnavailable(const QString &reason)
{
    if (!reason.isEmpty())
        qWarning("huruwave: %s", qUtf8Printable(reason));
    if (!m_available)
        return;
    m_available = false;
    emit availableChanged();
}

void Visualizer::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    if (active)
        start();
    else
        stop();
}

void Visualizer::start()
{
    if (m_process.state() != QProcess::NotRunning)
        return;

    const QString source = defaultMonitorSource();
    if (source.isEmpty()) {
        noteUnavailable(tr("no monitor source found; using the synthetic animation"));
        return;
    }
    // A different monitor (Bluetooth speakers, the Scarlett coming back, the
    // default sink moving) means the running config is stale, so it is rewritten
    // only when that actually changes rather than on every track.
    if (m_startedForSource != source) {
        if (!writeConfig(source)) {
            noteUnavailable(tr("could not write %1").arg(m_configPath));
            return;
        }
        m_startedForSource = source;
    }

    m_buffer.clear();
    m_process.setArguments({QStringLiteral("-p"), m_configPath});
    m_process.start();
    if (!m_process.waitForStarted(3000)) {
        noteUnavailable(tr("could not start %1").arg(m_binary));
        return;
    }
    if (!m_available) {
        m_available = true;
        emit availableChanged();
    }
}

void Visualizer::stop()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(500)) {
            m_process.kill();
            m_process.waitForFinished(500);
        }
    }
    m_buffer.clear();
    if (!m_values.isEmpty()) {
        // Clear the panel rather than freeze it on the last frame.
        m_values.clear();
        emit valuesChanged();
    }
    if (m_available) {
        m_available = false;
        emit availableChanged();
    }
}

bool Visualizer::writeConfig(const QString &source)
{
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());

    // Written whole every time. This file belongs to the app (it lives under the
    // app's own config directory), so nothing of the user's is overwritten, and
    // regenerating it keeps it correct when the output device changes.
    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << "# Generated by huruwave. Edits are overwritten.\n"
        << "[general]\n"
        << "bars = " << kBars << "\n"
        << "framerate = " << kFrameRate << "\n"
        << "\n"
        << "[input]\n"
        << "method = pulse\n"
        << "source = " << source << "\n"
        << "\n"
        << "[output]\n"
        << "method = raw\n"
        << "raw_target = /dev/stdout\n"
        << "data_format = ascii\n"
        << "ascii_max_range = " << kAsciiMaxRange << "\n"
        << "bar_delimiter = 59\n"
        << "frame_delimiter = 10\n";
    return true;
}

QString Visualizer::defaultMonitorSource()
{
    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), {QStringLiteral("info")});
    if (!pactl.waitForStarted(2000) || !pactl.waitForFinished(2000))
        return {};
    if (pactl.exitCode() != 0)
        return {};

    const QString output = QString::fromUtf8(pactl.readAllStandardOutput());
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QStringLiteral("Default Sink:")))
            continue;
        const QString sink = line.section(QLatin1Char(':'), 1).trimmed();
        if (sink.isEmpty())
            return {};
        // A monitor source is always the sink's name plus ".monitor".
        return sink.endsWith(QStringLiteral(".monitor")) ? sink : sink + QStringLiteral(".monitor");
    }
    return {};
}

QVector<double> Visualizer::parseFrame(const QByteArray &line)
{
    const QList<QByteArray> fields = line.trimmed().split(';');
    QVector<double> values;
    values.reserve(kBars);
    for (const QByteArray &field : fields) {
        const QByteArray trimmed = field.trimmed();
        if (trimmed.isEmpty())
            continue;  // cava ends each frame with a trailing delimiter
        bool ok = false;
        const double raw = trimmed.toDouble(&ok);
        if (!ok)
            return {};  // a garbled line is dropped, never half-drawn
        values.append(qBound(0.0, raw / kAsciiMaxRange, 1.0));
    }
    return values;
}

void Visualizer::handleData()
{
    m_buffer.append(m_process.readAllStandardOutput());

    bool changed = false;
    int newline = -1;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        const QVector<double> frame = parseFrame(line);
        if (frame.isEmpty())
            continue;

        m_values.clear();
        m_values.reserve(frame.size());
        for (double level : frame)
            m_values.append(level);
        changed = true;
    }

    if (changed)
        emit valuesChanged();
}
