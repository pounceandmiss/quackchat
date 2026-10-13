#include "EmojiModel.h"

#include <QFile>
#include <QFont>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QHash>
#include <QRawFont>
#include <QRegularExpression>
#include <QTextLayout>

#include <algorithm>

#include "AppSettings.h"

namespace {

// The groups in Unicode's order, which is also the file's.
const char *const kGroups[] = {"smileys", "people",  "nature",  "food",  "travel",
                               "activities", "objects", "symbols", "flags"};

struct Entry {
    QString emoji;
    QString group;
    QString name; // lowercased
    // Lowercased words of the name, then of the keywords: a search term
    // matches the start of any of them. nameWords says where the name's end.
    QStringList words;
    int nameWords = 0;
    QStringList variants;
};

struct Catalogue {
    QVector<Entry> entries;
    // Every form the catalogue holds, tones included, to its entry.
    QHash<QString, int> owner;
};

// Whether the font draws `emoji` as the one picture it stands for. Shaping is
// the only test that sees a sequence through: a font that has each part of a
// family or a flag but not the whole draws the parts side by side, every one
// of them a glyph it has. Invisible glyphs (the variation selector, a joiner
// shaped on its own) take no room and are not counted.
bool drawsAsOne(const QString &emoji, const QFont &font) {
    QTextLayout layout(emoji, font);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    int shown = 0;
    const QList<QGlyphRun> runs = layout.glyphRuns();
    for (const QGlyphRun &run : runs) {
        const QList<quint32> glyphs = run.glyphIndexes();
        if (glyphs.contains(0)) // .notdef: the box
            return false;
        const QList<QPointF> advances = run.rawFont().advancesForGlyphIndexes(glyphs);
        for (const QPointF &a : advances)
            if (a.x() > 0)
                ++shown;
    }
    return shown == 1;
}

Catalogue load() {
    Catalogue c;
    QFile file(QStringLiteral(":/emoji/emoji.tsv"));
    if (!file.open(QIODevice::ReadOnly))
        return c;

    // Only with fonts to ask: a test on a QCoreApplication has none, and
    // takes the list whole. So does a system with no emoji font at all,
    // where hiding everything would only make the picker look broken.
    const bool sift = qobject_cast<QGuiApplication *>(QCoreApplication::instance())
                      && drawsAsOne(QStringLiteral("\U0001F600"), QFont());
    const QFont font;
    const QRegularExpression wordBreak(QStringLiteral("[\\s:,\\-]+"));

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).chopped(1);
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() < 5)
            continue;
        if (sift && !drawsAsOne(f[1], font))
            continue;
        Entry e;
        e.group = f[0];
        e.emoji = f[1];
        e.name = f[2].toLower();
        e.words = e.name.split(wordBreak, Qt::SkipEmptyParts);
        e.nameWords = int(e.words.size());
        if (!f[3].isEmpty())
            e.words += f[3].toLower().split(wordBreak, Qt::SkipEmptyParts);
        e.words.removeDuplicates();
        for (const QString &v : f[4].split(QLatin1Char(' '), Qt::SkipEmptyParts))
            if (!sift || drawsAsOne(v, font))
                e.variants << v;
        // A square of pairs with a hole in it no longer reads as one: offer
        // what still lines up, the five shared tones, or nothing.
        if (e.variants.size() != 5 && e.variants.size() != 25)
            e.variants.clear();
        const int at = int(c.entries.size());
        c.owner.insert(e.emoji, at);
        for (const QString &v : std::as_const(e.variants))
            c.owner.insert(v, at);
        c.entries.push_back(std::move(e));
    }
    return c;
}

const Catalogue &catalogue() {
    static const Catalogue c = load();
    return c;
}

} // namespace

EmojiModel::EmojiModel(QObject *parent) : QAbstractListModel(parent) {
    rebuild();
}

// A function-local static, so a model built meanwhile waits for this one
// rather than building a second.
void EmojiModel::preload() {
    catalogue();
}

int EmojiModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant EmojiModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case HeaderRole:
        return row.header;
    case SectionRole:
        return row.section;
    case CellsRole: {
        QVariantList cells;
        for (int i = 0; i < row.entries.size(); ++i)
            cells << cell(row.entries.at(i), row.shown.at(i));
        return cells;
    }
    }
    return {};
}

QHash<int, QByteArray> EmojiModel::roleNames() const {
    return {{HeaderRole, "header"}, {SectionRole, "section"}, {CellsRole, "cells"}};
}

QVariantMap EmojiModel::cell(int entry, const QString &shown) const {
    const Entry &e = catalogue().entries.at(entry);
    return {{QStringLiteral("emoji"), shown},
            {QStringLiteral("base"), e.emoji},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("toned"), !e.variants.isEmpty()}};
}

void EmojiModel::setColumns(int columns) {
    columns = std::max(1, columns);
    if (m_columns == columns)
        return;
    m_columns = columns;
    emit columnsChanged();
    rebuild();
}

void EmojiModel::setFilter(const QString &filter) {
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

void EmojiModel::setSettings(AppSettings *settings) {
    if (m_settings == settings)
        return;
    if (m_settings)
        m_settings->disconnect(this);
    m_settings = settings;
    if (m_settings) {
        connect(m_settings, &AppSettings::recentEmojiChanged, this, &EmojiModel::rebuild);
        connect(m_settings, &AppSettings::emojiTonesChanged, this, &EmojiModel::rebuild);
    }
    emit settingsChanged();
    rebuild();
}

// The tone last picked for the entry, else the entry as Unicode draws it.
QString EmojiModel::preferred(int entry) const {
    const Catalogue &c = catalogue();
    if (m_settings)
        for (const QString &t : m_settings->emojiTones())
            if (c.owner.value(t, -1) == entry)
                return t;
    return c.entries.at(entry).emoji;
}

// Every word typed has to start one of the entry's words. Ranked by how well
// the name answers: all of it typed, every word typed a whole word of it
// ("joy" is the tears of joy before the joystick), its start, the start of one
// of its words, or only a keyword. Within a rank, what was used lately goes
// first, then the shorter name, being more nearly what was typed ("heart" is
// the red heart before the face with heart-eyes), then Unicode's order,
// keeping like with like.
QVector<int> EmojiModel::matches() const {
    const QString q = m_filter.trimmed().toLower();
    const QStringList terms =
        q.split(QRegularExpression(QStringLiteral("[\\s:,\\-]+")), Qt::SkipEmptyParts);
    if (terms.isEmpty())
        return {};
    const Catalogue &c = catalogue();
    QVector<QPair<int, int>> hits; // (rank, entry)
    for (int i = 0; i < c.entries.size(); ++i) {
        const Entry &e = c.entries.at(i);
        bool all = true;
        for (const QString &t : terms) {
            const bool some = std::any_of(e.words.cbegin(), e.words.cend(),
                                          [&](const QString &w) { return w.startsWith(t); });
            if (!some) {
                all = false;
                break;
            }
        }
        if (!all)
            continue;
        const auto nameBegin = e.words.cbegin();
        const auto nameEnd = e.words.cbegin() + e.nameWords;
        int rank = 4;
        if (e.name == q)
            rank = 0;
        else if (std::all_of(terms.cbegin(), terms.cend(), [&](const QString &t) {
                     return std::find(nameBegin, nameEnd, t) != nameEnd;
                 }))
            rank = 1;
        else if (e.name.startsWith(q))
            rank = 2;
        else if (std::any_of(nameBegin, nameEnd,
                             [&](const QString &w) { return w.startsWith(terms.first()); }))
            rank = 3;
        hits.push_back({rank, i});
    }
    // How lately each was used, sooner first; the rest after.
    QHash<int, int> used;
    if (m_settings) {
        const QStringList recent = m_settings->recentEmoji();
        for (int i = int(recent.size()) - 1; i >= 0; --i)
            used.insert(c.owner.value(recent.at(i), -1), i);
    }
    std::stable_sort(hits.begin(), hits.end(), [&](const auto &a, const auto &b) {
        if (a.first != b.first)
            return a.first < b.first;
        const int ua = used.value(a.second, INT_MAX), ub = used.value(b.second, INT_MAX);
        if (ua != ub)
            return ua < ub;
        return c.entries.at(a.second).nameWords < c.entries.at(b.second).nameWords;
    });
    QVector<int> out;
    out.reserve(hits.size());
    for (const auto &h : std::as_const(hits))
        out << h.second;
    return out;
}

void EmojiModel::rebuild() {
    const Catalogue &c = catalogue();
    QVector<Row> rows;

    // `entries` cut into rows of `columns`, drawn as `shownFor` says.
    const auto lay = [&](const QString &section, const QVector<int> &entries,
                         const QStringList &shown) {
        for (int i = 0; i < entries.size(); i += m_columns) {
            Row r;
            r.section = section;
            r.entries = entries.mid(i, m_columns);
            r.shown = shown.mid(i, m_columns);
            rows << r;
        }
    };
    const auto header = [&](const QString &section) {
        Row r;
        r.section = section;
        r.header = true;
        rows << r;
    };

    if (!m_filter.trimmed().isEmpty()) {
        const QVector<int> found = matches();
        QStringList shown;
        for (int e : found)
            shown << preferred(e);
        lay(QStringLiteral("search"), found, shown);
    } else {
        // Two rows of what was used last, as it was used: a tone stays the
        // tone it was sent in.
        QVector<int> recent;
        QStringList recentShown;
        if (m_settings)
            for (const QString &r : m_settings->recentEmoji()) {
                if (recent.size() >= 2 * m_columns)
                    break;
                const int at = c.owner.value(r, -1);
                if (at < 0)
                    continue;
                recent << at;
                recentShown << r;
            }
        if (!recent.isEmpty()) {
            header(QStringLiteral("recent"));
            lay(QStringLiteral("recent"), recent, recentShown);
        }
        for (const char *g : kGroups) {
            const QString group = QLatin1String(g);
            QVector<int> entries;
            QStringList shown;
            for (int i = 0; i < c.entries.size(); ++i)
                if (c.entries.at(i).group == group) {
                    entries << i;
                    shown << preferred(i);
                }
            if (entries.isEmpty())
                continue;
            header(group);
            lay(group, entries, shown);
        }
    }

    QVariantList sections;
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).header)
            sections << QVariantMap{{QStringLiteral("section"), rows.at(i).section},
                                    {QStringLiteral("row"), i}};

    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (sections != m_sections) {
        m_sections = sections;
        emit sectionsChanged();
    }
}

QStringList EmojiModel::variants(const QString &base) const {
    const Catalogue &c = catalogue();
    const int at = c.owner.value(base, -1);
    return at < 0 ? QStringList() : c.entries.at(at).variants;
}

QString EmojiModel::firstMatch() const {
    if (m_filter.trimmed().isEmpty() || m_rows.isEmpty() || m_rows.first().shown.isEmpty())
        return {};
    return m_rows.first().shown.first();
}

void EmojiModel::use(const QString &emoji) {
    if (!m_settings || emoji.isEmpty())
        return;
    QStringList recent = m_settings->recentEmoji();
    recent.removeAll(emoji);
    recent.prepend(emoji);
    m_settings->setRecentEmoji(recent.mid(0, kRecentKept));
}

void EmojiModel::useTone(const QString &base, const QString &emoji) {
    if (!m_settings)
        return;
    const Catalogue &c = catalogue();
    const int at = c.owner.value(base, -1);
    if (at < 0)
        return;
    QStringList tones = m_settings->emojiTones();
    tones.erase(std::remove_if(tones.begin(), tones.end(),
                               [&](const QString &t) { return c.owner.value(t, -1) == at; }),
                tones.end());
    if (emoji != c.entries.at(at).emoji)
        tones << emoji;
    m_settings->setEmojiTones(tones);
    use(emoji);
}
