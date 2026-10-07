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

QTEST_MAIN(TestI18n)
#include "tst_i18n.moc"
