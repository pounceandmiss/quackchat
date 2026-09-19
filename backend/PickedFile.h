// What a file dialog hands back, as a path tacky can open. Android gives a
// content:// url, readable only through Qt's file engine and named after a
// provider row, so it is copied into the cache under the document's own name.
// The extension is the point: tacky reads an attachment's kind from it, the
// upload slot is asked for a MIME derived from it, and the receiving client
// has only the url to go on.
#ifndef PICKEDFILE_H
#define PICKEDFILE_H

#include <QString>
#include <QUrl>

namespace pickedfile {

// Where files on their way out are kept: a copy of what a dialog picked, or
// a picture written out of the clipboard. "" if it cannot be made.
QString outgoingDir();

// A readable local path for `picked`, or "" if there is none.
QString localPath(const QUrl &picked);

// What the copy is called. Separate from the copying so it can be tested off
// a device: `displayName` and `mime` are what the platform said, both empty
// where there is nothing to ask.
QString nameFor(const QUrl &picked, const QString &displayName,
                const QString &mime);

} // namespace pickedfile

#endif // PICKEDFILE_H
