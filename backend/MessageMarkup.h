// Turns a message body plus tacky's XEP-0393 formatting spans into Qt rich
// text. Rendering, so it lives here rather than in the backend: tacky ships the
// display body with the styling characters already stripped and the spans that
// index into it, and every frontend draws that its own way.
#ifndef MESSAGEMARKUP_H
#define MESSAGEMARKUP_H

#include <QString>
#include <QVariantList>

// Spans are [{type, offset, length}], offsets in code points. `quoteColor` and
// `matchColor` are CSS colors for quoted runs and for the run a search matched,
// which the palette owns rather than this file; `linkColor` likewise for a
// `link` span, which becomes an anchor to the text it covers. Returns an empty
// string when nothing needs marking up, which is the caller's signal to draw
// the body as plain text and skip rich text entirely.
QString messageMarkup(const QString &body, const QVariantList &spans,
                      const QString &quoteColor,
                      const QString &matchColor = {},
                      const QString &linkColor = {});

// Whether a link span's text is one quack will hand to the system to open.
// tacky picks what counts as a link; this is the renderer's own check, since
// the text came from whoever sent the message.
bool isOpenableLink(const QString &url);

// One line of rich text for a search result: the body flattened to a single
// line with the matched runs bolded. `ranges` is tacky's `content.matches`,
// [{offset, length}] in the same code points `formatting` uses - where the
// query matched is the backend's answer, not something to work out again here.
// Always returns markup, unlike messageMarkup: the flattening is a change in
// its own right.
QString searchSnippet(const QString &body, const QVariantList &ranges);

#endif // MESSAGEMARKUP_H
