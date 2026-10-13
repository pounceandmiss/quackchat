// The emoji picker's rows: the catalogue in Unicode's order, cut to the width,
// searched, and led by what was used last with the tones that were picked.
#include <QtTest>
#include <QElapsedTimer>
#include <QGuiApplication>

#include "AppSettings.h"
#include "EmojiModel.h"
#include "TackyBackend.h"

namespace {

QVariantList cellsAt(const EmojiModel &m, int row) {
    return m.data(m.index(row), EmojiModel::CellsRole).toList();
}

bool isHeader(const EmojiModel &m, int row) {
    return m.data(m.index(row), EmojiModel::HeaderRole).toBool();
}

QStringList sectionKeys(const EmojiModel &m) {
    QStringList keys;
    for (const QVariant &s : m.sections())
        keys << s.toMap().value("section").toString();
    return keys;
}

// The first emoji drawn in `section`, as the grid shows it.
QString firstIn(const EmojiModel &m, const QString &section) {
    for (const QVariant &s : m.sections())
        if (s.toMap().value("section").toString() == section)
            return cellsAt(m, s.toMap().value("row").toInt() + 1)
                .first().toMap().value("emoji").toString();
    return {};
}

// Where `base` sits in the grid, as it is drawn there.
QString shownFor(const EmojiModel &m, const QString &base) {
    for (int r = 0; r < m.rowCount(); ++r)
        for (const QVariant &c : cellsAt(m, r))
            if (c.toMap().value("base").toString() == base
                && m.data(m.index(r), EmojiModel::SectionRole).toString() != "recent")
                return c.toMap().value("emoji").toString();
    return {};
}

} // namespace

class TestEmojiModel : public QObject {
    Q_OBJECT
private slots:
    void theGroupsComeInUnicodesOrder();
    void rowsAreCutToTheColumns();
    void searchPutsNameMatchesFirst();
    void tonesComeAsARowOrASquare();
    void recentsLeadAndATonePickSticks();
};

void TestEmojiModel::theGroupsComeInUnicodesOrder() {
    // Built off the UI thread, as the app does at startup: the font has to
    // answer there just as it does here.
    QElapsedTimer t;
    t.start();
    QThreadPool::globalInstance()->start(&EmojiModel::preload);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    qInfo("catalogue built in %lld ms", t.elapsed());

    EmojiModel m;

    QCOMPARE(sectionKeys(m), QStringList({"smileys", "people", "nature", "food", "travel",
                                          "activities", "objects", "symbols", "flags"}));
    QVERIFY(isHeader(m, 0));
    QCOMPARE(firstIn(m, "smileys"), QString("😀"));
    QCOMPARE(firstIn(m, "food"), QString("🍇"));
}

void TestEmojiModel::rowsAreCutToTheColumns() {
    EmojiModel m;
    m.setColumns(7);
    int emoji = 0;
    for (int r = 0; r < m.rowCount(); ++r) {
        if (isHeader(m, r))
            continue;
        const int n = int(cellsAt(m, r).size());
        QVERIFY(n >= 1 && n <= 7);
        // Only a group's last row runs short.
        if (r + 1 < m.rowCount() && !isHeader(m, r + 1))
            QCOMPARE(n, 7);
        emoji += n;
    }
    // Of the catalogue's 1923, as many as this system's emoji font draws.
    qInfo("%d emoji in the grid", emoji);
    QVERIFY(emoji > 1000);

    // The headers move with the width, and the tabs with them.
    const int before = m.sections().last().toMap().value("row").toInt();
    m.setColumns(14);
    QVERIFY(m.sections().last().toMap().value("row").toInt() < before);
}

void TestEmojiModel::searchPutsNameMatchesFirst() {
    EmojiModel m;
    m.setFilter("thumbs");
    QCOMPARE(m.firstMatch(), QString("👍"));
    QVERIFY(m.sections().isEmpty());
    QVERIFY(!isHeader(m, 0));

    // A word of the name beats a keyword: plenty of faces are tagged "joy".
    m.setFilter("joy");
    QCOMPARE(m.firstMatch(), QString("😂"));

    // The shorter name is nearer what was typed.
    m.setFilter("heart");
    QStringList found;
    for (int r = 0; r < m.rowCount(); ++r)
        for (const QVariant &c : cellsAt(m, r))
            found << c.toMap().value("emoji").toString();
    QVERIFY(found.indexOf("❤️") >= 0);
    QVERIFY(found.indexOf("❤️") < found.indexOf("😍"));

    // Every word has to land, each on the start of a word.
    m.setFilter("upside down");
    QCOMPARE(m.firstMatch(), QString("🙃"));
    m.setFilter("Down Upsi");
    QCOMPARE(m.firstMatch(), QString("🙃"));

    // Keywords count: nothing called "cheese" in its name.
    m.setFilter("cheese");
    QVERIFY(m.rowCount() > 0);

    m.setFilter("qqqzzz");
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.firstMatch(), QString());

    m.setFilter("");
    QCOMPARE(m.firstMatch(), QString());
    QVERIFY(isHeader(m, 0));
}

void TestEmojiModel::tonesComeAsARowOrASquare() {
    EmojiModel m;
    const QStringList thumbs = m.variants("👍");
    QCOMPARE(thumbs, QStringList({"👍🏻", "👍🏼", "👍🏽", "👍🏾", "👍🏿"}));
    // Asked by one of its own tones, the answer is the same.
    QCOMPARE(m.variants("👍🏾"), thumbs);
    QVERIFY(m.variants("😀").isEmpty());

    // A pair: a row per first person's tone, the matching ones on the
    // diagonal, whichever way Unicode spells them.
    const QStringList shake = m.variants("🤝");
    if (shake.size() == 5)
        QSKIP("the emoji font here has no mixed-tone handshakes");
    QCOMPARE(shake.size(), 25);
    QCOMPARE(shake.at(0), QString("🤝🏻"));
    QCOMPARE(shake.at(1), QString("🫱🏻‍🫲🏼"));
    QCOMPARE(shake.at(6), QString("🤝🏼"));
    QCOMPARE(shake.at(24), QString("🤝🏿"));
}

void TestEmojiModel::recentsLeadAndATonePickSticks() {
    TackyBackend backend; // never started: writes land locally all the same
    AppSettings settings;
    settings.setBackend(&backend);

    EmojiModel m;
    m.setColumns(8);
    m.setSettings(&settings);
    QCOMPARE(sectionKeys(m).first(), QString("smileys"));

    m.use("😂");
    m.use("🎉");
    m.use("😂");
    QCOMPARE(settings.recentEmoji(), QStringList({"😂", "🎉"}));
    QCOMPARE(sectionKeys(m).first(), QString("recent"));
    QCOMPARE(firstIn(m, "recent"), QString("😂"));

    // A tone picked is what the grid shows from then on, and a use.
    m.useTone("👍", "👍🏽");
    QCOMPARE(shownFor(m, "👍"), QString("👍🏽"));
    QCOMPARE(firstIn(m, "recent"), QString("👍🏽"));

    // Another replaces it rather than joining it.
    m.useTone("👍", "👍🏿");
    QCOMPARE(settings.emojiTones(), QStringList({"👍🏿"}));

    // The plain form takes the pick back.
    m.useTone("👍", "👍");
    QCOMPARE(shownFor(m, "👍"), QString("👍"));
    QVERIFY(settings.emojiTones().isEmpty());

    // A search puts what was used ahead of its equals: every heart answers
    // "heart" as well as the next, and the one this user sends is the one.
    m.setFilter("heart");
    QVERIFY(m.firstMatch() != QString("💚"));
    m.use("💚");
    QCOMPARE(m.firstMatch(), QString("💚"));
    m.setFilter("");

    // Two rows' worth, however many are kept.
    for (const QString &e : m.variants("👋") + m.variants("👏") + m.variants("🙌")
                                + QStringList{"😀", "😃", "😄"})
        m.use(e);
    int recentRows = 0;
    for (int r = 0; r < m.rowCount(); ++r)
        if (!isHeader(m, r) && m.data(m.index(r), EmojiModel::SectionRole) == "recent")
            ++recentRows;
    QCOMPARE(recentRows, 2);
    QVERIFY(settings.recentEmoji().size() <= EmojiModel::kRecentKept);
}

QTEST_MAIN(TestEmojiModel)
#include "tst_emojimodel.moc"
