// The translation pipeline end to end: that the catalogues CMake compiles are
// really in the binary, and that a string reaches them by the same route the
// app's own qsTr() calls take.
//
// Neither can be read off the sources. A .ts that never made it through
// lrelease, or a resource prefix that moved, leaves every qsTr() silently
// answering with its own argument - which is exactly what a correct English
// build looks like.
#include <QtTest>
#include <QCoreApplication>
#include <QTranslator>

class TestI18n : public QObject {
    Q_OBJECT
private slots:
    void theSourceCatalogueIsCompiledIntoTheBinary();
    void englishPluralsComeFromTheCatalogue();
    void aTranslationReachesTheStringItCovers();
};

void TestI18n::theSourceCatalogueIsCompiledIntoTheBinary() {
    QVERIFY(QFile::exists(QStringLiteral(":/i18n/quack_en.qm")));

    QTranslator t;
    QVERIFY(t.load(QStringLiteral("quack_en"), QStringLiteral(":/i18n")));
}

// "%n person(s)" is a placeholder for the two forms English needs, so the
// source catalogue has to be the one answering - not the source text.
void TestI18n::englishPluralsComeFromTheCatalogue() {
    QTranslator t;
    QVERIFY(t.load(QStringLiteral("quack_en"), QStringLiteral(":/i18n")));
    QVERIFY(QCoreApplication::installTranslator(&t));

    QCOMPARE(QCoreApplication::translate("MucDetailsPage", "%n person(s)", nullptr, 1),
             QString("1 person"));
    QCOMPARE(QCoreApplication::translate("MucDetailsPage", "%n person(s)", nullptr, 4),
             QString("4 people"));

    QVERIFY(QCoreApplication::removeTranslator(&t));
}

// A language nothing ships yet, built here rather than committed: what is being
// checked is that an installed catalogue is consulted at all, which no shipped
// English translation can show.
void TestI18n::aTranslationReachesTheStringItCovers() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ts = dir.filePath(QStringLiteral("quack_xx.ts"));
    const QString qm = dir.filePath(QStringLiteral("quack_xx.qm"));

    QFile f(ts);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"(<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS><TS version="2.1" language="xx">
<context><name>ConversationsPage</name>
<message><source>Chats</source><translation>Gesprekken</translation></message>
</context></TS>)");
    f.close();

    QProcess lrelease;
    lrelease.start(QLibraryInfo::path(QLibraryInfo::BinariesPath)
                       + QStringLiteral("/lrelease"),
                   {ts, QStringLiteral("-qm"), qm});
    QVERIFY2(lrelease.waitForFinished(30000), "lrelease did not finish");
    QCOMPARE(lrelease.exitCode(), 0);

    QTranslator t;
    QVERIFY(t.load(qm));
    QVERIFY(QCoreApplication::installTranslator(&t));
    QCOMPARE(QCoreApplication::translate("ConversationsPage", "Chats"),
             QString("Gesprekken"));
    QVERIFY(QCoreApplication::removeTranslator(&t));

    // And back to the source text once it is gone, so the check above was the
    // translator's doing and not the string's.
    QCOMPARE(QCoreApplication::translate("ConversationsPage", "Chats"),
             QString("Chats"));
}

QTEST_MAIN(TestI18n)
#include "tst_i18n.moc"
