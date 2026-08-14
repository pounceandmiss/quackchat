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
    // Not a paste: upstream chains its three dots with moves relative to a
    // closed subpath and they draw here as one. Same circles, absolute starts.
    readonly property string moreHoriz:
        "M4 12a2 2 0 1 0 4 0 2 2 0 1 0-4 0z" +
        "M10 12a2 2 0 1 0 4 0 2 2 0 1 0-4 0z" +
        "M16 12a2 2 0 1 0 4 0 2 2 0 1 0-4 0z"
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
    readonly property string search:
        "M15.5 14h-.79l-.28-.27C15.41 12.59 16 11.11 16 9.5 16 5.91 13.09 3 9.5 3S3 5.91 3 9.5" +
        " 5.91 16 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5z" +
        "m-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"

    // What a row, a menu or a bubble says about itself
    readonly property string group:
        "M16 11c1.66 0 2.99-1.34 2.99-3S17.66 5 16 5c-1.66 0-3 1.34-3 3s1.34 3 3 3z" +
        "m-8 0c1.66 0 2.99-1.34 2.99-3S9.66 5 8 5C6.34 5 5 6.34 5 8s1.34 3 3 3z" +
        "m0 2c-2.33 0-7 1.17-7 3.5V19h14v-2.5c0-2.33-4.67-3.5-7-3.5z" +
        "m8 0c-.29 0-.62.02-.97.05 1.16.84 1.97 1.97 1.97 3.45V19h6v-2.5c0-2.33-4.67-3.5-7-3.5z"
    readonly property string lock:
        "M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12" +
        "c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zm-6 9c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2z" +
        "m3.1-9H8.9V6c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2z"
    readonly property string lockOpen:
        "M12 17c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6-9h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6" +
        "h1.9c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2" +
        "h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zm0 12H6V10h12v10z"
    readonly property string image:
        "M21 19V5c0-1.1-.9-2-2-2H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2z" +
        "M8.5 13.5l2.5 3.01L14.5 12l4.5 6H5l3.5-4.5z"
    readonly property string attachFile:
        "M16.5 6v11.5c0 2.21-1.79 4-4 4s-4-1.79-4-4V5c0-1.38 1.12-2.5 2.5-2.5s2.5 1.12 2.5 2.5" +
        "v10.5c0 .55-.45 1-1 1s-1-.45-1-1V6H10v9.5c0 1.38 1.12 2.5 2.5 2.5s2.5-1.12 2.5-2.5V5" +
        "c0-2.21-1.79-4-4-4S7 2.79 7 5v12.5c0 3.04 2.46 5.5 5.5 5.5s5.5-2.46 5.5-5.5V6h-1.5z"

    // The call windows' endpoints, muted and not
    readonly property string mic:
        "M12 14c1.66 0 2.99-1.34 2.99-3L15 5c0-1.66-1.34-3-3-3S9 3.34 9 5v6c0 1.66 1.34 3 3 3z" +
        "m5.3-3c0 3-2.54 5.1-5.3 5.1S6.7 14 6.7 11H5c0 3.41 2.72 6.23 6 6.72V21h2v-3.28" +
        "c3.28-.48 6-3.3 6-6.72h-1.7z"
    readonly property string micOff:
        "M19 11h-1.7c0 .74-.16 1.43-.43 2.05l1.23 1.23c.56-.98.9-2.09.9-3.28z" +
        "m-4.02.17c0-.06.02-.11.02-.17V5c0-1.66-1.34-3-3-3S9 3.34 9 5v.18l5.98 5.99z" +
        "M4.27 3L3 4.27l6.01 6.01V11c0 1.66 1.33 3 2.99 3 .22 0 .44-.03.65-.08l1.66 1.66" +
        "c-.71.33-1.5.52-2.31.52-2.76 0-5.3-2.1-5.3-5.1H5c0 3.41 2.72 6.23 6 6.72V21h2v-3.28" +
        "c.91-.13 1.77-.45 2.54-.9L19.73 21 21 19.73 4.27 3z"
    readonly property string volumeUp:
        "M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02z" +
        "M14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77" +
        "s-2.99-7.86-7-8.77z"
    readonly property string volumeOff:
        "M16.5 12c0-1.77-1.02-3.29-2.5-4.03v2.21l2.45 2.45c.03-.2.05-.41.05-.63z" +
        "m2.5 0c0 .94-.2 1.82-.54 2.64l1.51 1.51C20.63 14.91 21 13.5 21 12c0-4.28-2.99-7.86-7-8.77" +
        "v2.06c2.89.86 5 3.54 5 6.71zM4.27 3L3 4.27 7.73 9H3v6h4l5 5v-6.73l4.25 4.25" +
        "c-.67.52-1.42.93-2.25 1.18v2.06c1.38-.31 2.63-.95 3.69-1.81L19.73 21 21 19.73l-9-9L4.27 3z" +
        "M12 4L9.91 6.09 12 8.18V4z"

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
