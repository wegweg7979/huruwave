import QtQuick

// The spectrum bars in the now-playing panel.
//
// Real data when cava is running -- `values` are band levels, 0..1, low
// frequency first, arriving about 30 times a second from the Visualizer -- and
// the synthetic animation this panel started life with when it is not, so the
// panel never looks dead on a box where cava is missing or failed to start.
Item {
    id: wave

    property bool playing: false
    property color colour: "#7aa2f7"
    property int bars: 24
    // Empty means "no analyser": draw the fallback animation instead.
    property var values: []

    readonly property bool analysing: values.length > 1

    Canvas {
        id: canvas
        anchors.fill: parent

        // Analyser frames drive their own repaint (they arrive by themselves);
        // the timer exists only for the fallback, which has to generate motion.
        Timer {
            interval: 50
            running: wave.playing && !wave.analysing
            repeat: true
            onTriggered: canvas.requestPaint()
        }

        Component.onCompleted: canvas.requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            const live = wave.analysing
            const count = live ? wave.values.length : wave.bars
            const gap = 3
            const barWidth = Math.max(2, (width - gap * (count - 1)) / count)
            const mid = height / 2

            // One phase per bar, so the fallback travels instead of pulsing in place.
            const phase = wave.playing ? Date.now() / 220 : 0

            for (let i = 0; i < count; ++i) {
                // Distance from the middle of the row, 0 at the edges, 1 centred.
                const centred = 1 - Math.abs((i / Math.max(1, count - 1)) * 2 - 1)

                let level
                if (live) {
                    level = wave.values[i]
                } else {
                    const wobble = 0.5 + 0.5 * Math.sin(phase + i * 0.55)
                    level = wave.playing ? (0.18 + 0.82 * wobble) * (0.35 + 0.65 * centred) : 0.06
                }

                // A floor keeps silence visible as a thin baseline rather than an
                // empty rectangle, and keeps a quiet passage from flickering off.
                const barHeight = Math.max(2, mid * Math.max(level, 0.04) * 1.7)

                ctx.fillStyle = wave.colour
                // Live bars brighten with their own energy; the fallback keeps the
                // steady centre-weighted glow.
                ctx.globalAlpha = live ? 0.4 + 0.6 * level
                                       : (wave.playing ? 0.35 + 0.65 * centred : 0.25)
                ctx.fillRect(i * (barWidth + gap), mid - barHeight / 2, barWidth, barHeight)
            }
            ctx.globalAlpha = 1
        }
    }

    onValuesChanged: canvas.requestPaint()
    onPlayingChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
