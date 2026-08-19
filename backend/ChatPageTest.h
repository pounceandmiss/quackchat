#ifndef CHATPAGETEST_H
#define CHATPAGETEST_H

// The fixture the chat-page suites work from: a real in-memory libtacky, and a
// real ChatWindow in an offscreen window - contentHeight and originY mean
// nothing without a laid-out viewport. Each suite seeds only the chats it opens.

#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>

#include <memory>

#include "AppController.h"
#include "ChatModel.h"
#include "QmlTestSupport.h"
#include "TackyBackend.h"

using namespace qmltest;

class ChatPageTest : public QObject {
protected:
    static inline const QString kAcc = QStringLiteral("me@example.com");
    static constexpr int kPage = 50;               // tacky's default history limit
    static constexpr int kSeeded = 2 * kPage + 40; // two whole local pages, then a short one
    static constexpr int kQuiet = 3;               // shorter than any viewport
    static inline const QString kOriginal = QStringLiteral("the original");
    // Long enough that a drag across the bubble lands mid-word at both ends.
    static inline const QString kSelectable =
            QStringLiteral("the quick brown fox jumps over the lazy dog");

    // A pop-out window on one chat, closed when it leaves scope.
    struct Chat {
        std::unique_ptr<QObject> window;
        QQuickItem *feed = nullptr;

        ChatModel *model() const {
            return qobject_cast<ChatModel *>(feed->property("model").value<QObject *>());
        }
        int count() const { return feed->property("count").toInt(); }
        qreal prop(const char *name) const { return feed->property(name).toReal(); }

        // olderBuffer()/newerBuffer() are QML functions, not properties, so the
        // scroll handler cannot read a stale copy of them.
        qreal buffer(const char *name) const {
            QVariant out;
            if (!QMetaObject::invokeMethod(feed, name, Q_RETURN_ARG(QVariant, out)))
                return qQNaN();
            return out.toReal();
        }

        QQuickWindow *win() const { return qobject_cast<QQuickWindow *>(window.get()); }

        // The delegate for one row. Every row carries the same objectNames, so
        // a search from the feed would answer with whichever it reached first.
        QQuickItem *row(int index) const {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(feed, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem *, item),
                                      Q_ARG(int, index));
            return item;
        }

        // Park the viewport `slack` pixels short of the oldest edge, reading
        // that edge afresh because a page that lands moves it.
        void scrollNearOldest(qreal slack = 0) const {
            feed->setProperty("contentY", prop("contentY") - buffer("olderBuffer") + slack);
        }
    };

    // In-memory session (no -transient 0), so nothing touches the real store.
    void startBackend() {
        m_engine = new QQmlEngine(this);
        m_app = m_engine->singletonInstance<AppController *>("Quack", "App");
        QVERIFY(m_app);
        QVERIFY(m_app->backend()->start());
        m_app->backend()->notify("account", "add",
                                 QVariantMap{{"acc", kAcc},
                                             {"password", "x"},
                                             {"domain", "example.com"},
                                             {"username", "me"}});
        m_component = new QQmlComponent(m_engine, "Quack", "ChatWindow", this);
        QVERIFY2(m_component->isReady(), qPrintable(m_component->errorString()));
    }

    void stopBackend() {
        if (m_app)
            m_app->backend()->stop();
    }

    void seed(const QString &jid, int n) {
        for (int i = 0; i < n; ++i)
            m_app->backend()->notify("message", "send",
                                     QVariantMap{{"acc", kAcc},
                                                 {"chat", jid},
                                                 {"body", QString("msg %1").arg(i)}});
    }

    void send(const QString &jid, const QString &body) {
        m_app->backend()->notify(
                "message", "send",
                QVariantMap{{"acc", kAcc}, {"chat", jid}, {"body", body}});
    }

    void setOmemo(const QString &jid, int on) {
        m_app->backend()->notify(
                "omemo", "setEnabled",
                QVariantMap{{"acc", kAcc}, {"jid", jid}, {"value", on}});
    }

    // What the store has for a chat, which is what a suite ends its seeding on.
    int stored(const QString &jid) {
        return call(*m_app->backend(), "message", "history",
                    QVariantMap{{"acc", kAcc}, {"chat", jid}, {"limit", 10 * kPage}})
                .toList()
                .size();
    }

    // Blocks until the backend answers. Requests queue behind the notifies
    // issued before them, so this doubles as a barrier for seeding.
    static QVariant call(TackyBackend &b, const QString &module, const QString &method,
                         const QVariantMap &args) {
        QSignalSpy done(&b, &TackyBackend::result);
        const int token = b.request(module, method, args);
        for (int i = 0; i < 100; ++i) {
            for (const QList<QVariant> &sig : std::as_const(done))
                if (sig.at(0).toInt() == token)
                    return sig.at(1);
            done.wait(200);
        }
        return {};
    }

    // Opens a chat from scratch: sessions are cached app-wide and shared by
    // every window on a chat, so one an earlier test left loaded would hand this
    // one a feed already full of history.
    Chat open(const QString &jid, bool groupchat = false) {
        m_app->forgetChat(kAcc, jid);
        return alsoOpen(jid, groupchat);
    }

    // A second window on a chat that is already open, sharing its session.
    Chat alsoOpen(const QString &jid, bool groupchat = false) {
        Chat chat;
        chat.window.reset(m_component->createWithInitialProperties(
            {{"account", kAcc},
             {"chatJid", jid},
             {"chatName", "Friend"},
             {"chatGroupchat", groupchat}}));
        if (auto *win = qobject_cast<QQuickWindow *>(chat.window.get()))
            chat.feed = win->findChild<QQuickItem *>("chatFeed");
        return chat;
    }

    // A popup declared inside a bubble is a QObject child of it, and the bubble
    // hangs off the visual tree with its delegate, so climb the parent items to
    // reach it.
    static QObject *popupIn(QQuickItem *from, const QString &name) {
        for (QQuickItem *at = from; at; at = at->parentItem())
            if (QObject *hit = at->findChild<QObject *>(name))
                return hit;
        return nullptr;
    }

    // The jump a search hit and a tapped quote both arrive by. No matches: what
    // a row marks is its own affair, and none of these ask it to mark anything.
    static bool jumpTo(QObject *page, qlonglong ts) {
        return QMetaObject::invokeMethod(page, "jumpTo", Q_ARG(QVariant, QVariant(ts)),
                                         Q_ARG(QVariant, QVariant(QVariantList{})));
    }

    // How the mouse picks words out of a message. The first line only: across a
    // wrap the release point would sit above the press point.
    static QString dragThroughWords(QQuickWindow *win, QQuickItem *body) {
        const QPointF left = body->mapToScene(QPointF(2, body->height() / 4));
        const QPointF right = body->mapToScene(QPointF(body->width() - 2, body->height() / 4));
        QTest::mousePress(win, Qt::LeftButton, {}, left.toPoint());
        QTest::mouseMove(win, QPointF((left.x() + right.x()) / 2, left.y()).toPoint());
        QTest::mouseMove(win, right.toPoint());
        QTest::mouseRelease(win, Qt::LeftButton, {}, right.toPoint());
        return body->property("selectedText").toString();
    }

    // Long enough for a page to land, so paging that should not happen has had
    // its chance to.
    static void settle() { QTest::qWait(400); }

    // The padlock is only drawn once the backend has said what the chat's
    // setting is, so anything reading or clicking it has to wait for that.
    static QQuickItem *shownLock(QQuickWindow *win) {
        auto *lock = win->findChild<QQuickItem *>("omemoToggle");
        if (lock && !QTest::qWaitFor(
                            [&] {
                                // Width as well: it lays out from zero as the
                                // control appears, and a coordinate taken
                                // before then misses.
                                return lock->property("visible").toBool() &&
                                       lock->width() > 0;
                            },
                            5000))
            qWarning("the padlock never drew");
        return lock;
    }

    QQmlEngine *m_engine = nullptr;
    AppController *m_app = nullptr;
    QQmlComponent *m_component = nullptr;
};

#endif // CHATPAGETEST_H
