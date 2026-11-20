import QtQuick

// An activity indicator, NOT a spectrum analyser: the bars are driven by playback
// state and the elapsed position, not by the audio signal. Feeding it real bands
// would mean tapping mpv's audio output, which is a separate piece of work -- so
// this is honest about being decoration, and reads as "something is playing"
// rather than pretending to measure anything.
Item {
    id: wave

    property bool playing: false
    property color colour: "#7aa2f7"
    property int bars: 24

    Canvas {
        id: canvas
        anchors.fill: parent

        // Repaint on a timer only while playing, so an idle window is silent.
        Timer {
            interval: 50
            running: wave.playing
            repeat: true
            onTriggered: canvas.requestPaint()
        }

        Component.onCompleted: canvas.requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const n = wave.bars
            const gap = 3
            const barWidth = Math.max(2, (width - gap * (n - 1)) / n)
            const mid = height / 2

            // One phase per bar, so the shape travels instead of pulsing in place.
            const phase = wave.playing ? Date.now() / 220 : 0

            for (let i = 0; i < n; ++i) {
                // A steady falloff towards the edges keeps the middle energetic.
                const centred = 1 - Math.abs((i / (n - 1)) * 2 - 1)
                const wobble = 0.5 + 0.5 * Math.sin(phase + i * 0.55)
                const level = wave.playing ? (0.18 + 0.82 * wobble) * (0.35 + 0.65 * centred) : 0.06
                const barHeight = Math.max(2, mid * level * 1.7)

                ctx.fillStyle = wave.colour
                ctx.globalAlpha = wave.playing ? 0.35 + 0.65 * centred : 0.25
                ctx.fillRect(i * (barWidth + gap), mid - barHeight / 2, barWidth, barHeight)
            }
            ctx.globalAlpha = 1
        }
    }

    onPlayingChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
