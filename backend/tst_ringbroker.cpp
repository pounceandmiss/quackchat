// The Android ring hand-off, run in one process: a broker serving memfds and a
// fetch through its socket.
#include <QtTest>

#include "ringbroker.h"

#include <cerrno>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int makeMemfd(const char *name, const QByteArray &content)
{
    const int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd >= 0 && write(fd, content.constData(), content.size()) != content.size()) {
        close(fd);
        return -1;
    }
    return fd;
}

static ino_t inode(int fd)
{
    struct stat st {};
    return fstat(fd, &st) == 0 ? st.st_ino : 0;
}

class TestRingBroker : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void handsOverTheNamedRing();
    void handsOverReadOnly();
    void refusesUnknownAndNonRingNames();

private:
    QByteArray m_socket;
};

void TestRingBroker::initTestCase()
{
    m_socket = "quack.test.frames." + QByteArray::number(getpid());
    QCOMPARE(ringbroker_start(m_socket.constData()), 0);
}

void TestRingBroker::handsOverTheNamedRing()
{
    const int longer = makeMemfd("tv-abcd", "longer");
    const int ring = makeMemfd("tv-abc", "ring");
    QVERIFY(longer >= 0 && ring >= 0);

    const int fd = ringbroker_fetch(m_socket.constData(), "tv-abc");
    QVERIFY(fd >= 0);
    QCOMPARE(inode(fd), inode(ring));
    char buf[8] = {};
    QCOMPARE(pread(fd, buf, sizeof(buf), 0), ssize_t(4));
    QCOMPARE(QByteArray(buf), QByteArray("ring"));

    close(fd);
    close(ring);
    close(longer);
}

void TestRingBroker::handsOverReadOnly()
{
    const int ring = makeMemfd("tv-0123", QByteArray(4096, 'x'));
    QVERIFY(ring >= 0);
    const int fd = ringbroker_fetch(m_socket.constData(), "tv-0123");
    QVERIFY(fd >= 0);

    QCOMPARE(write(fd, "y", 1), ssize_t(-1));
    QCOMPARE(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0), MAP_FAILED);
    QCOMPARE(errno, EACCES);

    close(fd);
    close(ring);
}

void TestRingBroker::refusesUnknownAndNonRingNames()
{
    const int other = makeMemfd("secret", "not a ring");
    QVERIFY(other >= 0);

    QCOMPARE(ringbroker_fetch(m_socket.constData(), "tv-ffff"), -1);
    QCOMPARE(ringbroker_fetch(m_socket.constData(), "secret"), -1);
    QCOMPARE(ringbroker_fetch(m_socket.constData(), "tv-../secret"), -1);
    QCOMPARE(ringbroker_fetch("quack.test.nobody", "tv-abc"), -1);

    close(other);
}

QTEST_MAIN(TestRingBroker)
#include "tst_ringbroker.moc"
