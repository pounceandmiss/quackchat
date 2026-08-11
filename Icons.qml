pragma Singleton
import QtQuick

// Material Icons path data, verbatim from google/material-design-icons
// (Apache-2.0), which publishes each icon at src/<group>/<name>/materialicons/
// 24px.svg. A property here is an upstream icon name with its underscores
// camel-cased; its value is that file's path, less the transparent 24x24 box
// upstream puts in front of every glyph. To add one, paste - do not draw.
//
// Paths rather than text glyphs because Android's fonts do not cover the symbol
// block dependably, and the send arrow rendered as tofu there. Rather than emoji
// because a colour font paints in its own palette and ignores the theme.
QtObject {
    // The composer, and the state a sent row reports back
    readonly property string send:
        "M2.01 21L23 12 2.01 3 2 10l15 2-15 2z"
    readonly property string reply:
        "M10 9V5l-7 7 7 7v-4.1c5 0 8.5 1.6 11 5.1-1-5-4-10-11-11z"
    readonly property string schedule:
        "M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2z" +
        "M12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8z" +
        "M12.5 7H11v6l5.25 3.15.75-1.23-4.5-2.67z"
    readonly property string check:
        "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z"
    readonly property string doneAll:
        "M18 7l-1.41-1.41-6.34 6.34 1.41 1.41L18 7zm4.24-1.41L11.66 16.17 7.48 12l-1.41 1.41L11.66 19" +
        "l12-12-1.42-1.41zM.41 13.41L6 19l1.41-1.41L1.83 12 .41 13.41z"

    // Headers and menus
    readonly property string close:
        "M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41" +
        " 17.59 19 19 17.59 13.41 12z"
    readonly property string moreHoriz:
        "M6 10c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm12 0c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z" +
        "m-6 0c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z"
    readonly property string openInNew:
        "M19 19H5V5h7V3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2v-7h-2v7z" +
        "M14 3v2h3.59l-9.83 9.83 1.41 1.41L19 6.41V10h2V3h-7z"
    readonly property string contentCopy:
        "M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1z" +
        "m3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z"
    readonly property string chevronLeft:
        "M15.41 7.41L14 6l-6 6 6 6 1.41-1.41L10.83 12z"
    readonly property string arrowBack:
        "M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z"
    readonly property string keyboardArrowUp:
        "M7.41 15.41L12 10.83l4.59 4.58L18 14l-6-6-6 6z"
    readonly property string keyboardArrowDown:
        "M7.41 8.59L12 13.17l4.59-4.58L18 10l-6 6-6-6 1.41-1.41z"
    readonly property string arrowDropDown:
        "M7 10l5 5 5-5z"
    readonly property string edit:
        "M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25z" +
        "M20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"
    readonly property string add:
        "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"

    // The handset pair every dialer draws: upright to answer, tipped over to
    // hang up. These two came in with the call windows and keep the older
    // call/call_end artwork they were pasted from, so those windows look as
    // they always did.
    readonly property string call:
        "M6.62 10.79c1.44 2.83 3.76 5.14 6.59 6.59l2.2-2.2c.27-.27.67-.36 1.02-.24" +
        " 1.12.37 2.33.57 3.57.57.55 0 1 .45 1 1V20c0 .55-.45 1-1 1-9.39 0-17-7.61-17-17" +
        " 0-.55.45-1 1-1h3.5c.55 0 1 .45 1 1 0 1.25.2 2.45.57 3.57.11.35.03.74-.25 1.02" +
        "l-2.2 2.2z"
    readonly property string callEnd:
        "M12 9c-1.6 0-3.15.25-4.6.72v3.1c0 .39-.23.74-.56.9-.98.49-1.87 1.12-2.66 1.85" +
        "-.18.18-.43.28-.7.28-.28 0-.53-.11-.71-.29L.29 13.08c-.18-.17-.29-.42-.29-.7" +
        " 0-.28.11-.53.29-.71C3.34 8.78 7.46 7 12 7s8.66 1.78 11.71 4.67c.18.18.29.43.29.71" +
        " 0 .28-.11.53-.29.71l-2.48 2.48c-.18.18-.43.29-.71.29-.27 0-.52-.11-.7-.28-.79-.74" +
        "-1.69-1.36-2.67-1.85-.33-.16-.56-.5-.56-.9v-3.1C15.15 9.25 13.6 9 12 9z"
}
