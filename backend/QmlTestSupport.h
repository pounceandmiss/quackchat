#ifndef QMLTESTSUPPORT_H
#define QMLTESTSUPPORT_H

// Shared by the QML suites: an engine that keeps its warnings, and the searches
// that reach into a scene the QObject tree does not describe.

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>

namespace qmltest {

// An engine that keeps the warnings it emits. A binding error fails nothing by
// itself, so a test that never reads them back goes green over a page that drew
// nothing. Every test drives one of these and ends by asking what it saw.
template <class Base>
class CollectingEngine : public Base {
public:
    CollectingEngine() {
        QObject::connect(this, &QQmlEngine::warnings,
                         [this](const QList<QQmlError> &ws) {
                             for (const QQmlError &w : ws)
                                 m_warnings << w.toString();
                         });
    }

    void assertNoErrors() const {
        for (const QString &w : m_warnings)
            if (w.contains("ReferenceError") || w.contains("is not defined") ||
                w.contains("TypeError") ||
                // A layout whose size depends on what it is sizing. The layout
                // gives up after two passes and leaves whatever it had, so this
                // is a real defect that otherwise only shows as a stray line on
                // stderr.
                w.contains("recursive rearrange") ||
                // A property that feeds itself. Qt breaks the cycle wherever
                // it notices, so what is on screen is whichever pass got there
                // last.
                w.contains("Binding loop"))
                QFAIL(qPrintable("QML error: " + w));
    }

private:
    QStringList m_warnings;
};

using Engine = CollectingEngine<QQmlEngine>;
using AppEngine = CollectingEngine<QQmlApplicationEngine>;

// Repeater and view delegates hang off the visual tree, not the QObject one, so
// findChild never sees them.
inline QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (!root)
        return nullptr;
    const auto children = root->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == name)
            return child;
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

// Every item under `root` named `name`, for the repeated parts of a list.
inline QList<QQuickItem *> findItems(QQuickItem *root, const QString &name) {
    QList<QQuickItem *> out;
    if (!root)
        return out;
    const auto children = root->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == name)
            out << child;
        out << findItems(child, name);
    }
    return out;
}

// Windows the app opens for itself are reached through the application rather
// than down any one window's tree: whichever window declared it, it is a
// top-level of its own.
inline QQuickWindow *windowNamed(const QString &objectName) {
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows)
        if (w->objectName() == objectName)
            return qobject_cast<QQuickWindow *>(w);
    return nullptr;
}

// Not that one opened, but how many did.
inline int windowsNamed(const QString &objectName) {
    int n = 0;
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows)
        if (w->objectName() == objectName)
            ++n;
    return n;
}

// Call windows are built for every row but shown one at a time, so what is on
// screen is the only question worth asking. topLevelWindows() counts the hidden
// ones too.
inline QList<QWindow *> visibleWindows() {
    QList<QWindow *> out;
    for (QWindow *w : QGuiApplication::topLevelWindows())
        if (w->isVisible())
            out << w;
    return out;
}

inline QWindow *visibleWindowTitled(const QString &fragment) {
    for (QWindow *w : visibleWindows())
        if (w->title().contains(fragment))
            return w;
    return nullptr;
}

// Where an item lands in some ancestor's coordinates, which is the only way to
// compare two that do not share a parent.
inline QRectF itemRect(QQuickItem *item, QQuickItem *within) {
    return item->mapRectToItem(within,
                               QRectF(0, 0, item->width(), item->height()));
}

// Did a frame for this method go out, whatever else did.
inline bool asked(const QSignalSpy &spy, const QString &module,
                  const QString &method) {
    for (const QList<QVariant> &call : spy)
        if (call.at(0).toString() == module && call.at(1).toString() == method)
            return true;
    return false;
}

// QTest::keyClicks is QWidget-only, and these presses have to reach a QWindow.
inline void type(QQuickWindow *win, const QString &text) {
    for (const QChar c : text)
        QTest::keyClick(win, c.toLatin1());
}

// The window AppWindows hands back for a second identical request is the one it
// already made: a second view of the same state would argue with the first over
// what is on screen. Asked from inside a test that holds a page of its own,
// since nothing here calls closeAll and AppWindows' window would otherwise be
// the last thing alive at teardown.
inline void oneWindowNotTwo(const QVariant &first, const QVariant &again) {
    QVERIFY(first.value<QObject *>());
    QCOMPARE(again.value<QObject *>(), first.value<QObject *>());
    QVERIFY(QMetaObject::invokeMethod(first.value<QObject *>(), "close"));
    QCoreApplication::processEvents();
}

} // namespace qmltest

#endif // QMLTESTSUPPORT_H
