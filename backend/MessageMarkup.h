// Turns a message body plus tacky's XEP-0393 formatting spans into Qt rich
// text. Rendering, so it lives here rather than in the backend: tacky ships the
// display body with the styling characters already stripped and the spans that
// index into it, and every frontend draws that its own way.
#ifndef MESSAGEMARKUP_H
#define MESSAGEMARKUP_H

#include <QString>
#include <QVariantList>

// Spans are [{type, offset, length}], offsets in code points. Returns an empty
// string when nothing needs marking up, which is the caller's signal to draw
// the body as plain text and skip rich text entirely.
QString messageMarkup(const QString &body, const QVariantList &spans);

#endif // MESSAGEMARKUP_H
