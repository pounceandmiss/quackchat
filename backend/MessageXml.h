// Lays a stored stanza out for the "View XML" viewer. Rendering, so it lives
// here rather than in the backend: tacky records the stanza as one long line,
// and its own Tk client pretty-prints in the GUI layer too (gui/xmlstream.tcl).
#ifndef MESSAGEXML_H
#define MESSAGEXML_H

#include <QString>

// One element per line, indented by depth. Prefixes and attribute order come
// through as written - this shows what was stored, it does not canonicalise it.
// Anything that will not parse (a truncated record, or something that was never
// a stanza) comes back untouched: showing it unformatted beats showing nothing.
QString formatMessageXml(const QString &stanza);

#endif // MESSAGEXML_H
