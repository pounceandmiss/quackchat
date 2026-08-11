pragma Singleton
import QtQuick

// Central theme / palette. Set `name` to any key in `palettes` to change the
// look; to add a theme, copy a block and give it new colors, keeping every key
// present (the UI reads them all). Ctrl+T cycles the themes in a live window.
QtObject {
    id: theme

    // The active theme. Change this to swap the whole app's colors.
    property string name: "conversations"

    // True on touch-first platforms. Desktop-only affordances (pop-out chat
    // windows, "New window") key off this, since Android/iOS are single-window.
    readonly property bool mobile: Qt.platform.os === "android" || Qt.platform.os === "ios"

    readonly property var palettes: ({
        // Light, warm "mallard duck" palette
        "duck": {
            background:  "#f7f1e6",   // app / chat wallpaper
            background2: "#efe6d2",   // wallpaper gradient end
            surface:     "#ffffff",   // header + input bar
            field:       "#f2ead9",   // message input pill
            hairline:    "#e7ddc8",   // thin dividers
            bubbleIn:    "#ffffff",   // incoming bubble
            bubbleOut:   "#ffe6b0",   // outgoing bubble (duck-bill amber)
            textPrimary: "#33302a",   // bubble + header text
            textDim:     "#a89a80",   // timestamps, secondary text
            accent:      "#f5a623",   // marigold, send button, avatar
            accent2:     "#ffca66",   // lighter accent (gradient top)
            accentDeep:  "#e07b00",   // pressed / icon accent
            positive:    "#0f9d76",   // mallard green, online, read ticks
            quote:       "#2f8f68",   // quoted lines (tacky keeps their "> " markers)
            negative:    "#d9534f",
            warning:     "#e0a325",
            textOnAccent:    "#5a3a00",   // text/icon drawn on an accent fill
            selection:   "#f5c14e",   // selected-message row highlight
            menuHover:   "#f7edd6"    // menu / toolbutton hover
        },
        // Light, teal-forward duck (the mallard's green head, barely any orange)
        "mallard": {
            background:  "#eef3f1",
            background2: "#e2ebe7",
            surface:     "#ffffff",
            field:       "#eaf1ee",
            hairline:    "#d9e4df",
            bubbleIn:    "#ffffff",
            bubbleOut:   "#d5ece2",   // pale mint
            textPrimary: "#2b332f",
            textDim:     "#93a49c",
            accent:      "#14a087",   // teal
            accent2:     "#4cc4ad",
            accentDeep:  "#0d7d69",
            positive:    "#e0a325",   // a small amber pop for read ticks
            quote:       "#0f8a6e",
            negative:    "#d9534f",
            warning:     "#e0a325",
            textOnAccent:    "#ffffff",
            selection:   "#7fd0be",
            menuHover:   "#e6f2ee"
        },
        // Light, cool blue
        "ocean": {
            background:  "#eef2f7",
            background2: "#e2e9f2",
            surface:     "#ffffff",
            field:       "#e9eef5",
            hairline:    "#d7e0ec",
            bubbleIn:    "#ffffff",
            bubbleOut:   "#d7e6fb",   // pale blue
            textPrimary: "#29323d",
            textDim:     "#93a1b3",
            accent:      "#3b82c4",
            accent2:     "#6aa6dd",
            accentDeep:  "#2c6aa6",
            positive:    "#2fae8f",
            quote:       "#2c8a66",
            negative:    "#d9534f",
            warning:     "#dda01f",
            textOnAccent:    "#ffffff",
            selection:   "#a9cdf2",
            menuHover:   "#e8f0f9"
        },
        // Light, soft rose
        "rose": {
            background:  "#faf1f2",
            background2: "#f3e3e6",
            surface:     "#ffffff",
            field:       "#f6e9eb",
            hairline:    "#ecd8dc",
            bubbleIn:    "#ffffff",
            bubbleOut:   "#fbdfe6",   // pale rose
            textPrimary: "#3a2f33",
            textDim:     "#b39aa1",
            accent:      "#d96a8f",
            accent2:     "#e892ac",
            accentDeep:  "#c14f77",
            positive:    "#4fae8a",
            quote:       "#3c8f6c",
            negative:    "#d1495b",
            warning:     "#d99a2b",
            textOnAccent:    "#ffffff",
            selection:   "#f3b9cb",
            menuHover:   "#f7e6ea"
        },
        // Light, muted sage
        "sage": {
            background:  "#f1f3ee",
            background2: "#e6ebe0",
            surface:     "#ffffff",
            field:       "#edf0e8",
            hairline:    "#dde3d5",
            bubbleIn:    "#ffffff",
            bubbleOut:   "#e2ecd6",   // pale sage
            textPrimary: "#333630",
            textDim:     "#9aa392",
            accent:      "#7a9a5f",   // olive/sage
            accent2:     "#9cb884",
            accentDeep:  "#5f7d47",
            positive:    "#4f9e7a",
            quote:       "#3d8759",
            negative:    "#c25450",
            warning:     "#d3a02c",
            textOnAccent:    "#ffffff",
            selection:   "#c3d6ac",
            menuHover:   "#eaefe2"
        },
        // WeChat-style: grey background, vivid green outgoing bubbles
        "wechat": {
            background:  "#ededed",
            background2: "#e6e6e6",
            surface:     "#f7f7f7",
            field:       "#ffffff",
            hairline:    "#d9d9d9",
            bubbleIn:    "#ffffff",
            bubbleOut:   "#95ec69",   // WeChat green
            textPrimary: "#191919",
            textDim:     "#8f8f8f",
            accent:      "#07c160",   // WeChat brand green
            accent2:     "#33d17a",
            accentDeep:  "#06a850",
            positive:    "#06a850",
            quote:       "#1e8a4c",
            negative:    "#fa5151",
            warning:     "#fa9d3b",
            textOnAccent:    "#ffffff",
            selection:   "#bdefa2",
            menuHover:   "#e6e6e6"
        },
        // Conversations / Material-style: blue-grey background, muted green bubbles
        "conversations": {
            background:  "#eceff1",
            background2: "#e2e7ea",
            surface:     "#ffffff",
            field:       "#eceff1",
            hairline:    "#dbe0e3",
            bubbleIn:    "#ffffff",
            bubbleOut:   "#dcedc8",   // material light-green 100
            textPrimary: "#263238",   // blue-grey 900
            textDim:     "#90a4ae",   // blue-grey 300
            accent:      "#43a047",   // green 600
            accent2:     "#66bb6a",   // green 400
            accentDeep:  "#2e7d32",   // green 800
            positive:    "#43a047",
            quote:       "#2e7d32",
            negative:    "#e53935",
            warning:     "#fb8c00",
            textOnAccent:    "#ffffff",
            selection:   "#c5e1a5",
            menuHover:   "#eef4e8"
        },
        // Cool, neutral dark palette
        "midnight": {
            background:  "#0e1621",
            background2: "#0b111a",
            surface:     "#17212b",
            field:       "#242f3d",
            hairline:    "#0a0f16",
            bubbleIn:    "#1e2b38",
            bubbleOut:   "#2b5278",
            textPrimary: "#e9eef2",
            textDim:     "#8fa5b0",
            accent:      "#5eb5f7",
            accent2:     "#7cc4fb",
            accentDeep:  "#2b7de9",
            positive:    "#4fc3f7",
            quote:       "#6fcf97",
            negative:    "#e5533d",
            warning:     "#e0a325",
            textOnAccent:    "#ffffff",
            selection:   "#3d6a99",
            menuHover:   "#22303c"
        },
        // Warm dark "plum" palette
        "plum": {
            background:  "#1c1322",
            background2: "#150e1a",
            surface:     "#271a30",
            field:       "#33243d",
            hairline:    "#100a15",
            bubbleIn:    "#33243d",
            bubbleOut:   "#6c3f7a",
            textPrimary: "#f0e9f4",
            textDim:     "#b39ec4",
            accent:      "#ff7a59",
            accent2:     "#ff9e86",
            accentDeep:  "#e85c3a",
            positive:    "#ff9e86",
            quote:       "#86d6a4",
            negative:    "#ff6b6b",
            warning:     "#ffb454",
            textOnAccent:    "#2a0f04",
            selection:   "#7a4d8f",
            menuHover:   "#33243d"
        }
    })

    // Active palette object; everything below re-reads it when `name` changes.
    readonly property var p: palettes[name]

    readonly property color background:  p.background
    readonly property color background2: p.background2
    readonly property color surface:     p.surface
    readonly property color field:       p.field
    readonly property color hairline:    p.hairline
    readonly property color bubbleIn:    p.bubbleIn
    readonly property color bubbleOut:   p.bubbleOut
    readonly property color textPrimary: p.textPrimary
    readonly property color textDim:     p.textDim
    readonly property color accent:      p.accent
    readonly property color accent2:     p.accent2
    readonly property color accentDeep:  p.accentDeep
    readonly property color positive:    p.positive
    readonly property color quote:       p.quote
    readonly property color negative:    p.negative
    readonly property color warning:     p.warning
    readonly property color textOnAccent:    p.textOnAccent
    readonly property color selection:   p.selection
    readonly property color menuHover:   p.menuHover

    // Cycle to the next theme (bound to Ctrl+T in every window).
    function cycle() {
        const keys = Object.keys(palettes)
        name = keys[(keys.indexOf(name) + 1) % keys.length]
    }
}
