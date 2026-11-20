#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QVector>

// Real spectrum bars, from cava.
//
// cava is the whole reason this is small: it is a ~90 KB binary that taps a
// PulseAudio/PipeWire monitor source, runs the FFT and prints bar heights. So
// this class is a process wrapper plus a line parser -- no DSP here, and no
// audio device handling either, which is what keeps it honest and short.
//
// The bars are BANDS, left to right low to high, normalized to 0..1. If cava is
// missing, cannot be started, or exits (a bad config is the usual cause) the
// window falls back to its synthetic animation rather than showing a dead panel
// -- see PulseWave.qml, which treats an empty value list that way.
//
// The monitor is resolved from `pactl info`'s default sink rather than left to
// cava's own default, because cava's default is the default SOURCE, which on
// these boxes is the microphone. A microphone-reactive "music visualiser" is
// wrong, so no resolvable monitor means no analyser at all.
class Visualizer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // Band levels, 0..1, low frequency first. Empty whenever there is nothing
    // real to show.
    Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged)

public:
    explicit Visualizer(QObject *parent = nullptr, const QString &binary = {}, const QString &configPath = {});
    ~Visualizer() override;

    // True while cava is running and feeding us frames.
    bool available() const { return m_available; }
    bool active() const { return m_active; }
    QVariantList values() const { return m_values; }

    // Starts cava when true, stops it when false (and clears the bars, so the
    // panel does not freeze on the last frame of the previous track).
    void setActive(bool active);

    // One cava frame -> normalized band levels. Public and static so the test
    // binary can exercise it: this is the only place a format change in cava
    // could bite, and silence about it would look like "the visualiser died".
    static QVector<double> parseFrame(const QByteArray &line);

    // The default sink's monitor source, e.g. "alsa_output.usb-....monitor".
    // Empty when it cannot be determined, which disables the analyser.
    static QString defaultMonitorSource();

    QString configPath() const { return m_configPath; }

signals:
    void availableChanged();
    void activeChanged();
    void valuesChanged();

private:
    void start();
    void stop();
    void noteUnavailable(const QString &reason);
    void handleData();
    bool writeConfig(const QString &source);

    QString m_binary;
    QString m_configPath;
    QString m_startedForSource;
    QProcess m_process;
    QByteArray m_buffer;
    QVariantList m_values;
    bool m_active = false;
    bool m_available = false;
};
