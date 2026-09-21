// The frame stream reader against a stand-in writer: frames split across
// writes, back-to-back in one, changing size, a longer header than this
// reader knows, and bytes that aren't a frame stream at all.
#include <QtTest>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include "FrameStream.h"

namespace {

QByteArray frame(int w, int h, char fill, qint64 pts, int headerLen = 40)
{
    const qsizetype plen = qsizetype(w) * h + 2 * (qsizetype((w + 1) / 2) * ((h + 1) / 2));
    QByteArray f(headerLen, '\0');
    auto *p = reinterpret_cast<uchar *>(f.data());
    qToLittleEndian<quint32>(0x31465654u, p);
    qToLittleEndian<quint16>(1, p + 4);
    qToLittleEndian<quint16>(quint16(headerLen), p + 6);
    qToLittleEndian<quint32>(0x30323449u, p + 8);
    qToLittleEndian<quint32>(1, p + 12);
    qToLittleEndian<quint32>(quint32(w), p + 16);
    qToLittleEndian<quint32>(quint32(h), p + 20);
    qToLittleEndian<quint32>(quint32(plen), p + 24);
    qToLittleEndian<qint64>(pts, p + 32);
    return f + QByteArray(plen, fill);
}

} // namespace

class TestFrameStream : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void aSplitFrameArrivesWhole();
    void aBatchShowsOnlyItsNewestFrame();
    void framesChangeSize();
    void aLongerHeaderIsSkipped();
    void garbageEndsTheStream();
    void theWriterGoingAwayEndsTheStream();

private:
    // Connects `stream` and returns the writer's end of it.
    QLocalSocket *connectStream(FrameStream &stream);

    QTemporaryDir m_dir;
    QLocalServer *m_server = nullptr;
};

void TestFrameStream::init()
{
    m_server = new QLocalServer(this);
    QVERIFY(m_server->listen(m_dir.filePath(QStringLiteral("tv-test.sock"))));
}

void TestFrameStream::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

QLocalSocket *TestFrameStream::connectStream(FrameStream &stream)
{
    stream.open(m_server->fullServerName());
    if (!m_server->waitForNewConnection(3000))
        return nullptr;
    return m_server->nextPendingConnection();
}

void TestFrameStream::aSplitFrameArrivesWhole()
{
    FrameStream stream;
    QSignalSpy ready(&stream, &FrameStream::frameReady);
    QLocalSocket *w = connectStream(stream);
    QVERIFY(w);
    const QByteArray f = frame(6, 4, 'a', 42);
    w->write(f.left(17));
    w->flush();
    QTest::qWait(50);
    QCOMPARE(ready.count(), 0);
    w->write(f.mid(17));
    w->flush();
    QVERIFY(ready.wait(3000));
    QCOMPARE(stream.latest().width, 6);
    QCOMPARE(stream.latest().height, 4);
    QCOMPARE(stream.latest().ptsNs, qint64(42));
    QVERIFY(stream.latest().keyframe);
    QCOMPARE(stream.latest().planes, QByteArray(6 * 4 + 2 * 3 * 2, 'a'));
}

void TestFrameStream::aBatchShowsOnlyItsNewestFrame()
{
    FrameStream stream;
    QSignalSpy ready(&stream, &FrameStream::frameReady);
    QLocalSocket *w = connectStream(stream);
    QVERIFY(w);
    w->write(frame(4, 2, 'a', 1) + frame(4, 2, 'b', 2) + frame(4, 2, 'c', 3));
    w->flush();
    QVERIFY(ready.wait(3000));
    QTRY_COMPARE(stream.latest().ptsNs, qint64(3));
    QCOMPARE(stream.latest().planes.at(0), 'c');
}

void TestFrameStream::framesChangeSize()
{
    FrameStream stream;
    QSignalSpy ready(&stream, &FrameStream::frameReady);
    QLocalSocket *w = connectStream(stream);
    QVERIFY(w);
    const QList<QSize> sizes{{320, 240}, {2560, 1440}, {17, 9}};
    for (const QSize &s : sizes) {
        w->write(frame(s.width(), s.height(), 'x', 0));
        w->flush();
        QTRY_VERIFY(stream.latest().width == s.width() && stream.latest().height == s.height());
    }
}

void TestFrameStream::aLongerHeaderIsSkipped()
{
    FrameStream stream;
    QSignalSpy ready(&stream, &FrameStream::frameReady);
    QLocalSocket *w = connectStream(stream);
    QVERIFY(w);
    w->write(frame(4, 2, 'z', 7, 56));
    w->flush();
    QVERIFY(ready.wait(3000));
    QCOMPARE(stream.latest().ptsNs, qint64(7));
    QCOMPARE(stream.latest().planes, QByteArray(4 * 2 + 2 * 2, 'z'));
}

void TestFrameStream::garbageEndsTheStream()
{
    FrameStream stream;
    QSignalSpy ended(&stream, &FrameStream::ended);
    QLocalSocket *w = connectStream(stream);
    QVERIFY(w);
    w->write(QByteArray(64, '\x7f'));
    w->flush();
    QVERIFY(ended.wait(3000));
    QVERIFY(!stream.isOpen());
}

void TestFrameStream::theWriterGoingAwayEndsTheStream()
{
    FrameStream stream;
    QSignalSpy ended(&stream, &FrameStream::ended);
    QLocalSocket *w = connectStream(stream);
    QVERIFY(w);
    w->disconnectFromServer();
    QVERIFY(ended.wait(3000));
}

QTEST_MAIN(TestFrameStream)
#include "tst_framestream.moc"
