pragma Singleton
// REDesk — design tokens (ADR-001 §3.5, AnyDesk-like look).
//
// Single source of truth for the palette / radii / spacing. The AnyDesk red is
// used tastefully as an accent only (primary actions, the "being controlled"
// pill), on an otherwise clean, modern, mostly-neutral surface.
import QtQuick

QtObject {
    // Accent (AnyDesk-ish red).
    readonly property color accent:        "#EF443B"
    readonly property color accentPressed: "#D23A32"
    readonly property color accentHover:   "#F25A52"

    // Surfaces (light, AnyDesk-like).
    readonly property color windowBg:   "#F4F5F7"
    readonly property color cardBg:     "#FFFFFF"
    readonly property color cardBorder: "#E5E7EB"
    readonly property color fieldBg:    "#F1F3F5"
    readonly property color fieldBorder:"#D9DEE3"

    // Dark surface for the remote video chrome.
    readonly property color videoBg:    "#0E1116"
    readonly property color toolbarBg:  "#1C2128"

    // Text.
    readonly property color textPrimary:   "#1F2933"
    readonly property color textSecondary: "#52606D"
    readonly property color textMuted:     "#8A97A4"
    readonly property color textOnAccent:  "#FFFFFF"
    readonly property color textOnDark:    "#E6EAF0"

    readonly property color success: "#2BA84A"

    // Geometry / rhythm.
    readonly property int radiusCard:  14
    readonly property int radiusInput: 10
    readonly property int radiusPill:  999

    readonly property int spacingXs: 6
    readonly property int spacingSm: 10
    readonly property int spacingMd: 16
    readonly property int spacingLg: 24
    readonly property int spacingXl: 32

    readonly property int fontSm: 12
    readonly property int fontMd: 14
    readonly property int fontLg: 18
    readonly property int fontXl: 26
}
