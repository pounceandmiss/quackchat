pragma Singleton
import QtQuick

// tacky counts time in microseconds; everywhere the GUI puts a clock face on
// one it comes through here, so a message and a search hit for it agree.
QtObject {
    // The time alone is enough for today. Older than that and the day has to
    // be said as well, or this morning is what it reads as; the year only once
    // it is not this one.
    function when(us) {
        if (!us)
            return ""
        const d = new Date(us / 1000)
        const hm = d.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        const now = new Date()
        if (d.toDateString() === now.toDateString())
            return hm
        const day = d.getFullYear() === now.getFullYear()
            //: Date of a message from an earlier day this year, e.g. "Aug 18"
            ? qsTr("MMM d")
            //: Date of a message from an earlier year, e.g. "Aug 18 2025"
            : qsTr("MMM d yyyy")
        //: Date then time on a message from an earlier day, e.g. "Aug 18 14:20"
        return qsTr("%1 %2").arg(d.toLocaleDateString(Qt.locale(), day)).arg(hm)
    }
}
