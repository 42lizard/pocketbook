import QtQuick

QtObject {
    required property real viewportWidth
    required property real viewportHeight

    readonly property real scale: Math.min(viewportWidth, viewportHeight) / 1404
    readonly property real smallGap: 16 * scale
    readonly property real gap: 24 * scale
    readonly property real largeGap: 32 * scale
    readonly property real chrome: 138 * scale
    readonly property real bookRow: 199 * scale
    readonly property real menuRow: 123 * scale
    readonly property real primaryAction: 134 * scale
    readonly property real framedInput: 139 * scale
    readonly property real footer: 70 * scale
    readonly property real titleFont: 44 * scale
    readonly property real bodyFont: 38 * scale
    readonly property real metaFont: 34 * scale
    readonly property real smallFont: 30 * scale
}
