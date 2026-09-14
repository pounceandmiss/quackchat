#include "FrameChannel.h"

#include <QLoggingCategory>

#include <atomic>
#include <cstring>

#ifndef Q_OS_WIN
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcVideo, "quack.video", QtWarningMsg)

namespace {

// Mirror of rtc-mv/src/rtcmv_internal.h.
constexpr uint32_t kMagic   = 0x44495654u; // 'T','V','I','D' LE
constexpr uint32_t kVersion = 1;
constexpr size_t   kHdrBytes = 4096;

struct RingHeader {
    std::atomic<uint64_t> writeIndex;
    uint32_t magic;
    uint32_t version;
    uint32_t slotCount;
    uint32_t slotBytes;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t format;
    uint32_t pad[8];
};

struct SlotHeader {
    std::atomic<uint64_t> seq;
    int64_t  ptsNs;
    uint32_t width, height;
    uint32_t strideY, strideUV;
    uint32_t payloadLen;
    uint32_t flags; // bit0 = keyframe
    uint32_t pad[2];
};

} // namespace

FrameChannel::~FrameChannel() { close(); }

void FrameChannel::close()
{
#ifndef Q_OS_WIN
    if (m_base) {
        ::munmap(m_base, m_mapBytes);
        m_base = nullptr;
    }
#endif
    m_mapBytes = m_slotBytes = 0;
    m_slots = 0;
    m_lastSeen = 0;
}

bool FrameChannel::openByName(const QString &shmName)
{
    close();
    if (shmName.isEmpty())
        return false;

#ifdef Q_OS_ANDROID
    // bionic has no shm_open/shm_unlink; the fd hand-off replacing this
    // (the "channel" token in <VideoTrack>/<VideoPreview>) isn't wired yet.
    qCWarning(lcVideo) << "no shm-by-name support on Android:" << shmName;
    return false;
#elif defined(Q_OS_WIN)
    // rtc-mv doesn't open a named CreateFileMappingW region on Windows yet.
    qCWarning(lcVideo) << "no shm-by-name support on Windows yet:" << shmName;
    return false;
#else
    const QByteArray n = shmName.toUtf8();
    int fd = ::shm_open(n.constData(), O_RDONLY, 0);
    if (fd < 0) {
        qCWarning(lcVideo) << "shm_open" << shmName << "failed:" << strerror(errno);
        return false;
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < kHdrBytes) {
        ::close(fd);
        return false;
    }
    void *base = ::mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (base == MAP_FAILED)
        return false;

    auto *h = static_cast<RingHeader *>(base);
    if (h->magic != kMagic || h->version != kVersion || h->slotCount == 0 ||
        h->slotBytes == 0) {
        ::munmap(base, st.st_size);
        qCWarning(lcVideo) << "not a valid frame ring:" << shmName;
        return false;
    }

    m_base = base;
    m_mapBytes = st.st_size;
    m_slotBytes = h->slotBytes;
    m_slots = h->slotCount;
    m_lastSeen = 0;
    qCDebug(lcVideo) << "opened ring" << shmName << h->maxWidth << "x" << h->maxHeight
                     << "slots" << h->slotCount;
    return true;
#endif
}

bool FrameChannel::read(Frame &out)
{
    if (!m_base)
        return false;

    auto *h = static_cast<RingHeader *>(m_base);
    const uint64_t wi = h->writeIndex.load(std::memory_order_acquire);
    if (wi == 0 || wi == m_lastSeen)
        return false;

    const uint32_t idx = static_cast<uint32_t>(wi % m_slots);
    auto *slot = static_cast<uint8_t *>(m_base) + kHdrBytes +
                 static_cast<size_t>(idx) * m_slotBytes;
    auto *sh = reinterpret_cast<SlotHeader *>(slot);

    const uint64_t s0 = sh->seq.load(std::memory_order_acquire);
    if (s0 != wi)
        return false; // mid-write

    const uint32_t plen = sh->payloadLen;
    if (plen == 0 || plen > m_slotBytes - sizeof(SlotHeader))
        return false;

    Frame f;
    f.seq = wi;
    f.ptsNs = sh->ptsNs;
    f.width = static_cast<int>(sh->width);
    f.height = static_cast<int>(sh->height);
    f.keyframe = (sh->flags & 1u) != 0;
    f.planes.resize(static_cast<int>(plen));
    std::memcpy(f.planes.data(), slot + sizeof(SlotHeader), plen);

    std::atomic_thread_fence(std::memory_order_acquire);
    const uint64_t s1 = sh->seq.load(std::memory_order_acquire);
    if (s1 != wi)
        return false; // lapped mid-copy

    out = std::move(f);
    m_lastSeen = wi;
    return true;
}
