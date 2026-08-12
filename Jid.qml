pragma Singleton
import QtQuick

// What a text field needs to know about a JID before there is anything to send:
// the bare, normalised form the chat list keys its rows by, and whether what has
// been typed could be a JID at all.
//
// tacky's lib/jid/jid.tcl is the authority on JIDs and stays that way - it is
// what normalises everything that is actually stored. But it is not on the wire,
// and asking would be a round trip per keystroke, so the two sheets that offer a
// JID box answer this much themselves. From here, once, rather than each.
QtObject {
    // Lower case and without resource or query, which is the form tacky stores
    // and the list keys by: typing a resource on would otherwise open a second
    // chat beside the one already there.
    function bare(text) {
        const raw = text.trim().toLowerCase()
        const cut = raw.search(/[\/?]/)
        return cut < 0 ? raw : raw.substring(0, cut)
    }

    // Enough to enable an OK button on: a localpart, an @, and a domain. Not
    // tacky's `jid valid-account`, which is stricter than a half-typed address
    // deserves to be judged by.
    function plausible(jid) {
        return /^[^@\s]+@[^@\s]+$/.test(jid)
    }
}
