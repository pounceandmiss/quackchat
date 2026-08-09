#include "MessageMarkup.h"

#include <QList>

namespace {

struct Span {
    int start = 0;  // code points, half-open [start, end)
    int end = 0;
    int rank = 0;   // block styles outermost, so they survive an inner split
    QString open;
    QString close;
};

// An unknown type gets no tags and draws as plain text, so a span kind added
// to tacky later degrades instead of leaking angle brackets into the bubble.
bool tagsFor(const QString &type, Span &s) {
    static const struct {
        const char *type;
        int rank;
        const char *open;
        const char *close;
    } kinds[] = {
        {"quote", 0, "<blockquote>", "</blockquote>"},
        {"preformatted", 1, "<pre>", "</pre>"},
        {"monospace", 2, "<span style=\"font-family:monospace\">", "</span>"},
        {"bold", 3, "<b>", "</b>"},
        {"italic", 4, "<i>", "</i>"},
        {"overstrike", 5, "<s>", "</s>"},
    };
    for (const auto &k : kinds) {
        if (type != QLatin1String(k.type))
            continue;
        s.rank = k.rank;
        s.open = QLatin1String(k.open);
        s.close = QLatin1String(k.close);
        return true;
    }
    return false;
}

// <pre> keeps its own whitespace; everywhere else HTML would eat the newlines
// and the runs of spaces a chat message is entitled to keep.
QString escaped(const QList<uint> &cps, int from, int to, bool preformatted) {
    QString out;
    bool afterSpace = false;
    for (int i = from; i < to; ++i) {
        const uint c = cps.at(i);
        switch (c) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '\n': out += preformatted ? QLatin1String("\n") : QLatin1String("<br>"); break;
        case ' ':
            out += (afterSpace && !preformatted) ? QLatin1String("&nbsp;")
                                                 : QLatin1String(" ");
            break;
        default:
            for (const QChar ch : QChar::fromUcs4(c))
                out += ch;
            break;
        }
        afterSpace = c == ' ';
    }
    return out;
}

} // namespace

QString messageMarkup(const QString &body, const QVariantList &spans) {
    if (body.isEmpty() || spans.isEmpty())
        return {};

    // tacky counts offsets in code points; QString indexes UTF-16, so a single
    // emoji ahead of a span would shift every tag one place left.
    const QList<uint> cps = body.toUcs4();
    const int n = cps.size();

    QList<Span> open;
    for (const QVariant &v : spans) {
        const QVariantMap m = v.toMap();
        Span s;
        if (!tagsFor(m.value(QStringLiteral("type")).toString(), s))
            continue;
        s.start = qBound(0, m.value(QStringLiteral("offset")).toInt(), n);
        s.end = qBound(s.start, s.start + m.value(QStringLiteral("length")).toInt(), n);
        if (s.end > s.start)
            open.append(s);
    }
    if (open.isEmpty())
        return {};

    // Longest first at a shared start, then block styles outward, so nesting
    // comes out stable and readable.
    std::sort(open.begin(), open.end(), [](const Span &a, const Span &b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.end != b.end) return a.end > b.end;
        return a.rank < b.rank;
    });

    QString out;
    QList<Span> stack;
    int next = 0; // next span in `open` to start
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
                out += stack.at(i).close;
                if (stack.at(i).end > pos)
                    reopen.prepend(stack.at(i));
            }
            stack.remove(ending, stack.size() - ending);
            for (const Span &s : std::as_const(reopen)) {
                out += s.open;
                stack.append(s);
            }
        }

        while (next < open.size() && open.at(next).start <= pos) {
            out += open.at(next).open;
            stack.append(open.at(next));
            ++next;
        }

        int until = n;
        for (const Span &s : std::as_const(stack))
            until = qMin(until, s.end);
        if (next < open.size())
            until = qMin(until, open.at(next).start);

        bool pre = false;
        for (const Span &s : std::as_const(stack))
            pre = pre || s.open == QLatin1String("<pre>");
        out += escaped(cps, pos, until, pre);
        pos = until;
    }
    for (int i = stack.size() - 1; i >= 0; --i)
        out += stack.at(i).close;
    return out;
}
