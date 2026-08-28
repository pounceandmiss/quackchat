// The pieces checked on their own, with no page around them.
#include <QtTest>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QScopeGuard>
#include <QStyleHints>

#include "AppController.h"
#include "RegistrationController.h"
#include "TackyBackend.h"
#include "MessageMarkup.h"

#include "QmlTestSupport.h"

using namespace qmltest;

class TestComponents : public QObject {
    Q_OBJECT

private slots:
    // Every colour in the active palette has to reach the property named after
    // it, and the failure is silent: a QML property whose name starts with "on"
    // followed by a capital reads as a signal handler, so `onAccent` never took
    // its initialiser and every icon drawn on an accent fill came out
    // default-constructed black.
    void themePropertiesCarryTheirPaletteColour() {
        Engine e;
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        const QVariantMap palette = theme->property("p").toMap();
        QVERIFY2(!palette.isEmpty(), "the active palette is empty");

        for (auto it = palette.cbegin(); it != palette.cend(); ++it) {
            const QColor want = QColor::fromString(it.value().toString());
            QVERIFY2(want.isValid(),
                     qPrintable(it.key() + " is not a colour: " + it.value().toString()));
            const QVariant got = theme->property(it.key().toUtf8().constData());
            QVERIFY2(got.isValid(),
                     qPrintable("Theme has no property for palette key " + it.key()));
            QVERIFY2(got.value<QColor>() == want,
                     qPrintable(QStringLiteral("Theme.%1 is %2, palette says %3")
                                    .arg(it.key(), got.value<QColor>().name(),
                                         want.name())));
        }
    }

    // Adding a colour means editing every palette block, and missing one only
    // fails when somebody switches to that theme - at which point the property
    // reads as an invalid colour and whatever it painted comes out black. So
    // check the key sets match rather than trusting nine hand edits.
    void everyPaletteCarriesTheSameKeys() {
        Engine e;
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        const QVariantMap palettes = theme->property("palettes").toMap();
        QVERIFY2(palettes.size() > 1, "expected several palettes to compare");

        // QVariantMap keys come back sorted, so this compares as sets.
        const QStringList want = palettes.first().toMap().keys();
        QVERIFY(!want.isEmpty());
        for (auto it = palettes.cbegin(); it != palettes.cend(); ++it) {
            const QStringList got = it.value().toMap().keys();
            QVERIFY2(got == want,
                     qPrintable(QStringLiteral("palette \"%1\" has [%2], expected [%3]")
                                    .arg(it.key(), got.join(", "), want.join(", "))));
        }
    }

    // The two palettes the system's preferences name have to be palettes, and
    // the failure is the silent one above: an unknown key leaves `p` undefined
    // and every colour reads black. Renaming one breaks it from a third file.
    void theSystemPalettesAreRealPalettes() {
        Engine e;
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        const QVariantMap palettes = theme->property("palettes").toMap();
        const QString light = theme->property("systemLight").toString();
        const QString dark = theme->property("systemDark").toString();
        QVERIFY2(palettes.contains(light), qPrintable("no palette named " + light));
        QVERIFY2(palettes.contains(dark), qPrintable("no palette named " + dark));
        QVERIFY(light != dark);
    }

    // The app starts on whichever of the two the system asks for and follows it
    // while it runs, so a window already open repaints. A palette picked by
    // hand stops that, the pick being an answer to the same question.
    //
    // The system's half only runs where there is a platform theme to ask:
    // QStyleHints::setColorScheme goes through one, and the offscreen platform
    // these tests use has none, so there the scheme is Unknown and immovable.
    void themeFollowsTheDesktopUntilOneIsPicked() {
        Engine e;
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        const QString light = theme->property("systemLight").toString();
        const QString dark = theme->property("systemDark").toString();

        QVERIFY(theme->property("followSystem").toBool());
        QCOMPARE(theme->property("name").toString(), theme->property("systemName").toString());

        QStyleHints *hints = QGuiApplication::styleHints();
        const Qt::ColorScheme was = hints->colorScheme();
        auto restore = qScopeGuard([&] { hints->setColorScheme(was); });
        hints->setColorScheme(Qt::ColorScheme::Dark);
        const bool schemeIsOurs = hints->colorScheme() == Qt::ColorScheme::Dark;
        if (schemeIsOurs) {
            QCOMPARE(theme->property("name").toString(), dark);
            hints->setColorScheme(Qt::ColorScheme::Light);
            QCOMPARE(theme->property("name").toString(), light);
            // No preference is not a preference for dark.
            hints->setColorScheme(Qt::ColorScheme::Unknown);
            QCOMPARE(theme->property("name").toString(), light);
        }

        // A pick pins the palette against anything the system says after.
        QVERIFY(QMetaObject::invokeMethod(theme, "choose", Q_ARG(QVariant, QString("plum"))));
        QVERIFY(!theme->property("followSystem").toBool());
        QCOMPARE(theme->property("name").toString(), QString("plum"));
        if (schemeIsOurs) {
            hints->setColorScheme(Qt::ColorScheme::Dark);
            QCOMPARE(theme->property("name").toString(), QString("plum"));
        }

        // Ctrl+T cycles from whatever is showing, picking as it goes.
        theme->setProperty("followSystem", true);
        const QString showing = theme->property("name").toString();
        QVERIFY(QMetaObject::invokeMethod(theme, "cycle"));
        QVERIFY(!theme->property("followSystem").toBool());
        QCOMPARE(theme->property("name").toString(), theme->property("chosen").toString());
        QVERIFY(theme->property("name").toString() != showing);
    }

    // The device list is built from a plain JS array that gets replaced whole
    // every time devices are re-enumerated, so the menu is filled by an
    // Instantiator rather than declared. Check it really populates, that the
    // tick tracks the current id rather than a position, and that picking
    // reports the id instead of the label.
    void deviceMenuListsEveryDeviceAndReportsThePick() {
        Engine e;
        QQuickWindow win;
        win.resize(400, 120);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "AudioLevelRow");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        auto *row = qobject_cast<QQuickItem *>(comp.createWithInitialProperties(
            {{"label", "Microphone"},
             {"devices",
              QVariantList{QVariantMap{{"name", "Built-in"}, {"id", "mic-1"}},
                           QVariantMap{{"name", "USB headset"}, {"id", "mic-2"}}}},
             {"deviceId", "mic-2"},
             {"width", win.width()}}));
        QVERIFY2(row, qPrintable(comp.errorString()));
        row->setParentItem(win.contentItem());

        QObject *menu = row->findChild<QObject *>("deviceMenu");
        QVERIFY(menu);
        // The two real devices, behind the system-default entry.
        QTRY_COMPARE(menu->property("count").toInt(), 3);

        auto itemAt = [&](int i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item),
                                      Q_ARG(int, i));
            return item;
        };
        QVERIFY(itemAt(2));
        QVERIFY(!itemAt(0)->property("current").toBool());
        QVERIFY(!itemAt(1)->property("current").toBool());
        QVERIFY(itemAt(2)->property("current").toBool());

        QSignalSpy picked(row, SIGNAL(devicePicked(QString)));
        QVERIFY(QMetaObject::invokeMethod(itemAt(1), "triggered"));
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked.first().at(0).toString(), QString("mic-1"));

        // "" is a real choice, not an empty one: it hands the pick back to the
        // system rather than leaving the device alone.
        QVERIFY(QMetaObject::invokeMethod(itemAt(0), "triggered"));
        QCOMPARE(picked.count(), 2);
        QCOMPARE(picked.at(1).at(0).toString(), QString());

        e.assertNoErrors();
    }

    // Android lays the keyboard over the window instead of resizing it, so a
    // dialog centres in the strip left above it. The rectangle Android reports
    // is in physical pixels, and clamped against a window measured in
    // device-independent ones it read as the whole window - leaving the sheet
    // dead centre under the keyboard. Nothing raises a keyboard on a headless
    // platform, so the top edge is placed by hand.
    void aDialogCentresInTheStripTheKeyboardLeaves() {
        Engine e;

        QQuickWindow win;
        win.resize(360, 800);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "AccountRail");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"width", win.width()}, {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *rail = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(rail);
        rail->setParentItem(win.contentItem());

        QObject *sheet = rail->findChild<QObject *>("addAccountSheet");
        QVERIFY(sheet);
        QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
        QTRY_VERIFY(sheet->property("opened").toBool());

        const qreal h = sheet->property("height").toReal();
        // Short enough that a keyboard over the bottom half still leaves a
        // strip it fits in, or the top margin is what would be measured below.
        QVERIFY2(h > 0 && h < 380, qPrintable(QString("sheet is %1 high").arg(h)));

        // Keyboard down: dead centre of the window.
        QCOMPARE(sheet->property("keyboardTop").toReal(), qreal(-1));
        QTRY_COMPARE(sheet->property("y").toReal(),
                     qreal(qRound((win.height() - h) / 2)));

        // Keyboard up over the bottom half: centred in what is left of the
        // window, and wholly above the keyboard.
        sheet->setProperty("keyboardTop", 400);
        QTRY_COMPARE(sheet->property("y").toReal(), qreal(qRound((400 - h) / 2)));
        QVERIFY(sheet->property("y").toReal() + h <= 400);

        // A strip too short to hold it: pinned under the top margin rather than
        // centred off the top of the window.
        sheet->setProperty("keyboardTop", 40);
        QTRY_COMPARE(sheet->property("y").toReal(), qreal(12));

        e.assertNoErrors();
    }

    // A registration form is drawn from questions the app has never seen: the
    // field types decide which control answers each one, and nothing about the
    // page can be checked against a layout written for it. So: every field got
    // a control, of the kind its type calls for, laid out down the page.
    void drawsWhateverFormTheServerAsked() {
        Engine e;
        e.singletonInstance<AppController *>("Quack", "App");

        QQuickWindow win;
        win.resize(360, 600);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        // Declared before the form it feeds, so it outlives it.
        TackyBackend backend;
        RegistrationController reg;
        reg.setBackend(&backend); // unstarted: a read gets a token and no reply
        reg.applyForm(QJsonDocument::fromJson(R"({
            "instructions":"Pick a name",
            "fields":[
                {"var":"FORM_TYPE","type":"hidden","value":["jabber:iq:register"]},
                {"var":"username","type":"text-single","label":"Username",
                 "required":true,"value":[]},
                {"var":"password","type":"text-private","label":"Password",
                 "required":true,"value":[]},
                {"var":"tier","type":"list-single","label":"Tier","value":["paid"],
                 "options":[{"label":"Free","value":"free"},
                            {"label":"Paid","value":"paid"}]},
                {"var":"terms","type":"boolean","label":"I agree","value":["0"]},
                {"var":"ocr","type":"text-single","label":"Type the word",
                 "required":true,"value":[],
                 "media":{"cid":"c1","type":"image/png"}}
            ]})")
                          .object()
                          .toVariantMap());
        QCOMPARE(reg.rowCount(), 5);

        QQmlComponent comp(&e, "Quack", "DataForm");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"fields", QVariant::fromValue(&reg)}, {"width", win.width()}}));
        QVERIFY(!obj.isNull());
        auto *form = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(form);
        form->setParentItem(win.contentItem());
        win.grabWindow(); // force the delegates to lay out and bind
        QCoreApplication::processEvents();

        // The control each type calls for, and no other: a row that builds one
        // of each and shows one is how a delegate covers six field types.
        QQuickItem *user = findItem(win.contentItem(), "formLine0");
        QQuickItem *secret = findItem(win.contentItem(), "formLine1");
        QQuickItem *tier = findItem(win.contentItem(), "formChoice2");
        QQuickItem *terms = findItem(win.contentItem(), "formTick3");
        QVERIFY(user && secret && tier && terms);
        QVERIFY(user->isVisible() && secret->isVisible());
        QVERIFY(tier->isVisible() && terms->isVisible());
        QVERIFY(!findItem(win.contentItem(), "formChoice0")->isVisible());
        // The one the password goes in is the one that hides it.
        QCOMPARE(secret->property("echoMode").toInt(), 2); // TextInput.Password
        // A list field opens on what the server preselected.
        QCOMPARE(tier->property("currentText").toString(), QString("Paid"));

        // Laid out down the page, each inside the form and none over another:
        // a form that stacked its fields at zero height would still find every
        // control above and read as drawn.
        QRectF last;
        for (const QString &name :
             {"formLine0", "formLine1", "formChoice2", "formTick3", "formLine4"}) {
            QQuickItem *item = findItem(win.contentItem(), name);
            QVERIFY2(item, qPrintable(name));
            const QRectF r = itemRect(item, form);
            QVERIFY2(r.width() > 0 && r.height() > 0, qPrintable(name));
            QVERIFY2(r.left() >= 0 && r.right() <= form->width() + 1, qPrintable(name));
            QVERIFY2(r.top() >= last.bottom(), qPrintable(name));
            last = r;
        }
        QVERIFY(form->height() >= last.bottom());

        // Typing an answer writes it back through the model, which is where a
        // form's state lives: nothing is ever collected off the controls.
        user->forceActiveFocus();
        for (const QChar c : QStringLiteral("alice"))
            QTest::keyClick(&win, c.toLatin1());
        QTRY_COMPARE(reg.valueFor("username"), QString("alice"));

        // The CAPTCHA is a further round trip, so its field is drawn before
        // there is a picture for it: nothing until the bytes turn up, then the
        // picture itself, straight out of a data: URL.
        QQuickItem *picture = findItem(win.contentItem(), "formMedia4");
        QVERIFY(picture);
        QVERIFY(!picture->isVisible());
        QSignalSpy sent(&backend, &TackyBackend::sent);
        reg.handleEvent("register", "MediaReady",
                        QVariantMap{{"token", reg.token()}, {"var", "ocr"}});
        QVERIFY(asked(sent, "register", "media"));
        // The first read this backend has been asked for, so token 1.
        reg.handleResult(1, QString("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ"
                                    "AAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5"
                                    "ErkJggg=="));
        QTRY_VERIFY(picture->isVisible());
        QTRY_COMPARE(picture->property("status").toInt(), 1); // Image.Ready
        QVERIFY(picture->width() > 0 && picture->height() > 0);

        e.assertNoErrors();
    }

    // A stamp from today is a time and nothing else; an older one says which
    // day it was, and one from a year that is not this one says the year too.
    void aStampSaysTheDayOnceItIsNotToday() {
        Engine e;
        QQmlComponent comp(&e);
        comp.setData(R"(import QtQuick
import Quack
QtObject { function when(us) { return Stamp.when(us) } })",
                     QUrl(QStringLiteral("qrc:/tst_stamp.qml")));
        QScopedPointer<QObject> probe(comp.create());
        QVERIFY2(probe, qPrintable(comp.errorString()));

        const auto when = [&probe](const QDateTime &t) {
            QVariant out;
            const qint64 us = t.toMSecsSinceEpoch() * 1000;
            QMetaObject::invokeMethod(probe.get(), "when", Q_RETURN_ARG(QVariant, out),
                                      Q_ARG(QVariant, QVariant(us)));
            return out.toString();
        };

        const QLocale loc;
        const QDate today = QDate::currentDate();
        const QTime at(14, 20);
        const QString hm = loc.toString(at, QLocale::ShortFormat);
        QCOMPARE(when(QDateTime(today, at)), hm);

        // Mid-year, so what stands for "this year" is never a day either side
        // of a New Year the test happens to run over.
        QDate other(today.year(), 6, 15);
        if (other == today)
            other = other.addDays(1);
        QCOMPARE(when(QDateTime(other, at)),
                 loc.toString(other, QStringLiteral("MMM d")) + " " + hm);
        QCOMPARE(when(QDateTime(other.addYears(-1), at)),
                 loc.toString(other.addYears(-1), QStringLiteral("MMM d yyyy")) + " " + hm);

        // A row is keyed by its stamp so one is always there, but blank beats
        // 1970 for whatever gets asked before it has one.
        QCOMPARE(when(QDateTime::fromMSecsSinceEpoch(0)), QString());

        e.assertNoErrors();
    }

    // A pasted traceback is preformatted, and <pre> alone marks its lines
    // unbreakable: the bubble stayed its usual width while the words carried
    // on straight out through the side of it.
    void aPreformattedBodyStaysInsideItsBubble() {
        Engine e;
        QQuickWindow win;
        win.resize(500, 400);
        win.show();
        QVERIFY2(QTest::qWaitForWindowExposed(&win), "the window never appeared");

        const QString body = QStringLiteral(
            "Traceback (most recent call last):\n"
            "  File \"/usr/lib/python3.14/site-packages/gajim/gtk/preview/image.py\", "
            "line 220, in _on_thumbnail\n"
            "    thumbnail_bytes, _metadata = future.result()");
        const QVariantList spans{QVariantMap{{"type", "preformatted"},
                                             {"offset", 0},
                                             {"length", body.size()}}};

        QQmlComponent comp(&e, "Quack", "ChatBubble");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QQuickItem> bubble(qobject_cast<QQuickItem *>(
            comp.createWithInitialProperties(
                {{"text", body},
                 {"markup", messageMarkup(body, spans, "#0a0")},
                 {"width", win.width()}})));
        QVERIFY(!bubble.isNull());
        bubble->setParentItem(win.contentItem());

        QQuickItem *text = findItem(bubble.data(), "bubbleText");
        QVERIFY(text);
        QTRY_VERIFY(text->width() > 0);
        // What it draws, against the room it was given. A line too long to
        // break would run past both.
        QVERIFY2(text->property("contentWidth").toReal() <= text->width(),
                 qPrintable(QStringLiteral("%1 of words in %2 of bubble")
                                .arg(text->property("contentWidth").toReal())
                                .arg(text->width())));

        e.assertNoErrors();
    }
};

QTEST_MAIN(TestComponents)
#include "tst_qmlload_components.moc"
