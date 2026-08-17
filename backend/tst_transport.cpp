// SocketTransport against a real QLocalServer standing in for the Android
// backend service. Covers the lenpipe framing both ways, since that codec has a
// Tcl original (lib/lenpipe/lenpipe.tcl) and a Java twin in the service.
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QtTest>

#include "CallsModel.h"
#include "ChatListModel.h"
#include "SocketTransport.h"
#include "TackyBackend.h"

class TestTransport : public QObject {
    Q_OBJECT

    QLocalServer m_server;
    QLocalSocket *m_peer = nullptr; // the service end of the accepted connection
    QByteArray m_peerBuf;
    QString m_name;

    // Abstract namespace, matching what the service binds on Android.
    void listen() {
        m_server.setSocketOptions(QLocalServer::AbstractNamespaceOption);
        QVERIFY(m_server.listen(m_name));
    }

    // Accepts the pending connection and stashes the service end. Spins the
    // event loop rather than blocking in it: the transport's own retry timer
    // has to run for a reconnect to ever arrive.
    bool accept() {
        const bool came = QTest::qWaitFor(
            [this] { return m_server.hasPendingConnections(); }, 3000);
        if (!came)
            return false;
        m_peer = m_server.nextPendingConnection();
        m_peerBuf.clear();
        return m_peer != nullptr;
    }

    // Everything the service end has received so far. Accumulates, because a
    // frame can be split across reads here just as it is on the other side.
    QByteArray peerBytes() {
        if (m_peer)
            m_peerBuf += m_peer->readAll();
        return m_peerBuf;
    }

    static QByteArray frame(const QByteArray &payload) {
        return QByteArray::number(payload.size()) + '\n' + payload;
    }

private slots:
    void init() {
        m_peer = nullptr;
        // Unique per test: a lingering transport from an earlier one must not
        // be able to connect to this one's server.
        m_name = QStringLiteral("quack-tst-%1-%2")
                     .arg(QCoreApplication::applicationPid())
                     .arg(QTest::currentTestFunction());
        m_server.close();
        listen();
    }

    void cleanup() { m_server.close(); }

    // One frame in, one frame out, with the payload handed over intact.
    void roundTripsAFrame() {
        SocketTransport t(m_name);
        t.setRetryInterval(20);
        QSignalSpy got(&t, &SocketTransport::received);
        QVERIFY(t.start({}));
        QVERIFY(accept());
        QTRY_VERIFY(t.isConnected());

        m_peer->write(frame(R"(["result",1,[]])"));
        QTRY_COMPARE(got.count(), 1);
        QCOMPARE(got.first().at(0).toString(), QString(R"(["result",1,[]])"));

        t.send(R"(["account","list",{},1])");
        QTRY_COMPARE(peerBytes(), frame(R"(["account","list",{},1])"));
    }

    // The length prefix counts UTF-8 bytes, not characters. Getting this wrong
    // desynchronises the stream with no way to resynchronise.
    void lengthIsInBytesNotCharacters() {
        SocketTransport t(m_name);
        t.setRetryInterval(20);
        QSignalSpy got(&t, &SocketTransport::received);
        QVERIFY(t.start({}));
        QVERIFY(accept());
        QTRY_VERIFY(t.isConnected());

        const QByteArray payload = QString::fromUtf8("[\"é\",\"日\"]").toUtf8();
        QVERIFY(payload.size() > QString::fromUtf8(payload).size());
        m_peer->write(frame(payload));
        QTRY_COMPARE(got.count(), 1);
        QCOMPARE(got.first().at(0).toString(), QString::fromUtf8(payload));

        t.send(payload);
        QTRY_COMPARE(peerBytes(), frame(payload));
    }

    // Sockets split wherever they like: a frame can arrive a byte at a time,
    // and several can arrive in one read.
    void reassemblesSplitAndCoalescedFrames() {
        SocketTransport t(m_name);
        t.setRetryInterval(20);
        QSignalSpy got(&t, &SocketTransport::received);
        QVERIFY(t.start({}));
        QVERIFY(accept());
        QTRY_VERIFY(t.isConnected());

        const QByteArray split = frame(R"(["event","conn","Ready",{}])");
        for (int i = 0; i < split.size(); ++i) {
            m_peer->write(split.mid(i, 1));
            m_peer->flush();
        }
        QTRY_COMPARE(got.count(), 1);

        m_peer->write(frame("[1]") + frame("[2]") + frame("[3]"));
        QTRY_COMPARE(got.count(), 4);
        QCOMPARE(got.at(2).at(0).toString(), QString("[2]"));
        QCOMPARE(got.at(3).at(0).toString(), QString("[3]"));
    }

    // The service dying must not be terminal: the UI reconnects on its own.
    void reconnectsAfterTheServiceDrops() {
        SocketTransport t(m_name);
        t.setRetryInterval(20);
        QSignalSpy state(&t, &SocketTransport::connectedChanged);
        QVERIFY(t.start({}));
        QVERIFY(accept());
        QTRY_VERIFY(t.isConnected());

        m_peer->abort();
        QTRY_VERIFY(!t.isConnected());

        QVERIFY(accept()); // the retry timer brings it back
        QTRY_VERIFY(t.isConnected());
        QVERIFY(state.count() >= 3); // up, down, up

        QSignalSpy got(&t, &SocketTransport::received);
        m_peer->write(frame("[1]"));
        QTRY_COMPARE(got.count(), 1); // framing state survived the drop
    }

    // Nothing queues behind a dead link; callers re-sync on the next edge.
    void sendWhileDisconnectedIsDropped() {
        SocketTransport t(m_name);
        t.setRetryInterval(20);
        QVERIFY(t.start({}));
        QVERIFY(accept());
        QTRY_VERIFY(t.isConnected());
        m_peer->abort();
        QTRY_VERIFY(!t.isConnected());

        t.send(R"(["account","list",{}])"); // must not crash or buffer
        QVERIFY(accept());
        QTRY_VERIFY(t.isConnected());
        QTest::qWait(100);
        QCOMPARE(peerBytes(), QByteArray());
    }

    // TackyBackend over a socket keeps its contract: running follows the link,
    // connected() fires on the rising edge, and frames decode as usual.
    void backendDrivesTheSocketTransport() {
        TackyBackend b;
        auto *t = new SocketTransport(m_name);
        t->setRetryInterval(20);
        b.setTransport(t);
        QSignalSpy up(&b, &TackyBackend::connected);
        QSignalSpy ev(&b, &TackyBackend::event);
        QVERIFY(b.start());
        QVERIFY(accept());
        QTRY_VERIFY(b.isRunning());
        QCOMPARE(up.count(), 1);

        m_peer->write(frame(R"(["event","conn","Ready",{"acc":"me@h"}])"));
        QTRY_COMPARE(ev.count(), 1);
        QCOMPARE(ev.first().at(0).toString(), QString("conn"));
        QCOMPARE(ev.first().at(1).toString(), QString("Ready"));

        QCOMPARE(b.request("account", "list"), 1); // token space is unchanged
        QTRY_VERIFY(peerBytes().contains(R"(["account","list",{},1])"));
    }

    // A model bound before the link is up asks into the void: its tokens are
    // allocated but nothing carries them. The connected edge is what makes it
    // ask again, and it has to fire on a reconnect too, not just the first time.
    void modelsResyncOnEveryConnectedEdge() {
        TackyBackend b;
        auto *t = new SocketTransport(m_name);
        t->setRetryInterval(20);
        b.setTransport(t);

        ChatListModel chats;
        chats.setBackend(&b); // binds while disconnected
        chats.setAccount("me@h");

        QSignalSpy sent(&b, &TackyBackend::sent);
        QVERIFY(b.start());
        QVERIFY(accept());
        QTRY_VERIFY(b.isRunning());
        QTRY_VERIFY(asked(sent, "chatlist", "get"));

        sent.clear();
        m_peer->abort();
        QTRY_VERIFY(!b.isRunning());
        QVERIFY(accept());
        QTRY_VERIFY(b.isRunning());
        QTRY_VERIFY(asked(sent, "chatlist", "get"));
    }

    // Calls re-seed off conn rather than the connected edge, so the reconnect
    // has to carry a `calls list` through that route too.
    void callsResyncWhenAnAccountReportsConnected() {
        TackyBackend b;
        auto *t = new SocketTransport(m_name);
        t->setRetryInterval(20);
        b.setTransport(t);

        CallsModel calls;
        calls.setBackend(&b);

        QSignalSpy sent(&b, &TackyBackend::sent);
        QVERIFY(b.start());
        QVERIFY(accept());
        QTRY_VERIFY(b.isRunning());

        // What AccountsModel's `conn pull` brings back on every attach.
        emit b.event("conn", "State",
                     QVariantMap{{"acc", "me@h"}, {"state", "connected"}});
        QTRY_VERIFY(asked(sent, "calls", "list"));

        sent.clear();
        m_peer->abort();
        QTRY_VERIFY(!b.isRunning());
        QVERIFY(accept());
        QTRY_VERIFY(b.isRunning());
        emit b.event("conn", "State",
                     QVariantMap{{"acc", "me@h"}, {"state", "connected"}});
        QTRY_VERIFY(asked(sent, "calls", "list"));
    }

private:
    static bool asked(const QSignalSpy &spy, const QString &module,
                      const QString &method) {
        for (const QList<QVariant> &call : spy)
            if (call.at(0).toString() == module && call.at(1).toString() == method)
                return true;
        return false;
    }
};

QTEST_MAIN(TestTransport)
#include "tst_transport.moc"
