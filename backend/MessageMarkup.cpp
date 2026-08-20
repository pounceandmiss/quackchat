#include "MessageMarkup.h"

#include <QList>

#include <optional>

namespace {

struct Span {
    int start = 0; // code points, half-open [start, end)
    int end = 0;
    // Lower nests further out, so a block style survives an inline one splitting
    // inside it rather than breaking into one block per styled stretch.
    int rank = 0;
    bool literalWhitespace = false;
    QString openTag;
    QString closeTag;
};

// An unknown type gets no tags and draws as plain text, so a kind added to
// tacky later degrades instead of leaking angle brackets into the bubble.
//
// A quote is colored rather than indented: tacky leaves the "> " markers in the
// body, so <blockquote> would stack Qt's own indent and block break on top of
// markers that already set the run apart.
std::optional<Span> spanFor(const QString &type, const QString &quoteColor,
                            const QString &matchColor) {
    Span s;
    if (type == QLatin1String("match")) {
        // Innermost, so the run keeps whatever styling it was written with and
        // only gains the wash behind it.
        s.rank = 6;
        s.openTag = QStringLiteral("<span style=\"background-color:%1\">").arg(matchColor);
        s.closeTag = QStringLiteral("</span>");
    } else if (type == QLatin1String("quote")) {
        s.rank = 0;
        s.openTag = QStringLiteral("<span style=\"color:%1\">").arg(quoteColor);
        s.closeTag = QStringLiteral("</span>");
    } else if (type == QLatin1String("preformatted")) {
        s.rank = 1;
        s.literalWhitespace = true;
        // pre-wrap rather than <pre>'s own pre: both keep the whitespace, but
        // pre marks the lines unbreakable, and a pasted traceback then paints
        // its full width straight out through the side of the bubble.
        s.openTag = QStringLiteral("<pre style=\"white-space:pre-wrap\">");
        s.closeTag = QStringLiteral("</pre>");
    } else if (type == QLatin1String("monospace")) {
        s.rank = 2;
        s.openTag = QStringLiteral("<span style=\"font-family:monospace\">");
        s.closeTag = QStringLiteral("</span>");
    } else if (type == QLatin1String("bold")) {
        s.rank = 3;
        s.openTag = QStringLiteral("<b>");
        s.closeTag = QStringLiteral("</b>");
    } else if (type == QLatin1String("italic")) {
        s.rank = 4;
        s.openTag = QStringLiteral("<i>");
        s.closeTag = QStringLiteral("</i>");
    } else if (type == QLatin1String("overstrike")) {
        s.rank = 5;
        s.openTag = QStringLiteral("<s>");
        s.closeTag = QStringLiteral("</s>");
    } else {
        return {};
    }
    return s;
}

// <pre> keeps its own whitespace; everywhere else HTML would eat the newlines
// and the runs of spaces a chat message is entitled to keep. `from` may land
// mid-run, so the character before it decides whether this one is a repeat.
QString escaped(const QList<uint> &cps, int from, int to, bool literal) {
    QString out;
    bool afterSpace = from > 0 && cps.at(from - 1) == ' ';
    for (int i = from; i < to; ++i) {
        const uint c = cps.at(i);
        switch (c) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '\n': out += literal ? QLatin1String("\n") : QLatin1String("<br>"); break;
        case ' ':
            out += (afterSpace && !literal) ? QLatin1String("&nbsp;") : QLatin1String(" ");
            break;
        default:
            for (QChar ch : QChar::fromUcs4(c))
                out += ch;
            break;
        }
        afterSpace = c == ' ';
    }
    return out;
}

// Snippet text keeps none of a bubble's whitespace, so this is all the escaping
// it needs.
QString escapedPlain(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c == u'&')
            out += QLatin1String("&amp;");
        else if (c == u'<')
            out += QLatin1String("&lt;");
        else if (c == u'>')
            out += QLatin1String("&gt;");
        else
            out += c;
    }
    return out;
}

} // namespace

QString messageMarkup(const QString &body, const QVariantList &spans,
                      const QString &quoteColor, const QString &matchColor) {
    if (body.isEmpty() || spans.isEmpty())
        return {};

    // tacky counts offsets in code points; QString indexes UTF-16, so a single
    // emoji ahead of a span would shift every tag one place left.
    const QList<uint> cps = body.toUcs4();
    const int n = cps.size();

    QList<Span> ordered;
    for (const QVariant &v : spans) {
        const QVariantMap m = v.toMap();
        std::optional<Span> s = spanFor(m.value(QStringLiteral("type")).toString(),
                                        quoteColor, matchColor);
        if (!s)
            continue;
        s->start = qBound(0, m.value(QStringLiteral("offset")).toInt(), n);
        s->end = qBound(s->start, s->start + m.value(QStringLiteral("length")).toInt(), n);
        if (s->end > s->start)
            ordered.append(*s);
    }
    if (ordered.isEmpty())
        return {};

    // Longest first at a shared start, then outermost rank, so the nesting is
    // the same every time for a given set of spans.
    std::sort(ordered.begin(), ordered.end(), [](const Span &a, const Span &b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.end != b.end) return a.end > b.end;
        return a.rank < b.rank;
    });

    QString out;
    QList<Span> stack;
    int nextSpan = 0;
    int pos = 0;
    while (pos < n) {
        // Close what ends here. Spans may overlap without nesting, so anything
        // still running above the closed one is shut and reopened around it.
        int ending = -1;
        for (int i = 0; i < stack.size(); ++i)
            if (stack.at(i).end <= pos) { ending = i; break; }
        if (ending >= 0) {
            QList<Span> reopen;
            for (int i = stack.size() - 1; i >= ending; --i) {
                out += stack.at(i).closeTag;
                if (stack.at(i).end > pos)
                    reopen.prepend(stack.at(i));
            }
            stack.remove(ending, stack.size() - ending);
            for (const Span &s : std::as_const(reopen)) {
                out += s.openTag;
                stack.append(s);
            }
        }

        while (nextSpan < ordered.size() && ordered.at(nextSpan).start <= pos) {
            out += ordered.at(nextSpan).openTag;
            stack.append(ordered.at(nextSpan));
            ++nextSpan;
        }

        // Run to whichever comes first: a span ending or the next one opening.
        int until = n;
        bool literal = false;
        for (const Span &s : std::as_const(stack)) {
            until = qMin(until, s.end);
            literal = literal || s.literalWhitespace;
        }
        if (nextSpan < ordered.size())
            until = qMin(until, ordered.at(nextSpan).start);

        out += escaped(cps, pos, until, literal);
        pos = until;
    }
    for (int i = stack.size() - 1; i >= 0; --i)
        out += stack.at(i).closeTag;
    return out;
}

// The ranges index the body as it stands, so the whitespace this collapses has
// to go as the walk passes it: simplify() first and every offset behind the
// first double space would be wrong.
QString searchSnippet(const QString &body, const QVariantList &ranges) {
    const QList<uint> cps = body.toUcs4();
    const int n = cps.size();

    // Painted rather than kept as pairs, so ranges that touch or overlap become
    // one run instead of nesting a tag inside its own kind.
    QList<bool> matched(n, false);
    for (const QVariant &v : ranges) {
        const QVariantMap m = v.toMap();
        const int from = qBound(0, m.value(QStringLiteral("offset")).toInt(), n);
        const int to =
            qBound(from, from + m.value(QStringLiteral("length")).toInt(), n);
        for (int i = from; i < to; ++i)
            matched[i] = true;
    }

    QString out;
    bool open = false;
    bool held = false;  // whitespace passed over, not yet drawn
    bool drawn = false; // something before it, so the space has a left side
    for (int i = 0; i < n; ++i) {
        const uint c = cps.at(i);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            held = true;
            continue;
        }
        // Closing before the held space and opening after it keeps the mark off
        // the gap at either end, while a space between two matched characters
        // stays inside the run.
        if (open && !matched.at(i)) {
            out += QLatin1String("</b>");
            open = false;
        }
        if (held && drawn)
            out += QLatin1Char(' ');
        held = false;
        if (!open && matched.at(i)) {
            out += QLatin1String("<b>");
            open = true;
        }
        for (QChar ch : QChar::fromUcs4(c))
            out += escapedPlain(QString(ch));
        drawn = true;
    }
    if (open)
        out += QLatin1String("</b>");
    return out;
}
