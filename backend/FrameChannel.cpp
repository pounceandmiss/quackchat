#include "FrameChannel.h"

#include <QLoggingCategory>

#include <atomic>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include "ringbroker.h"

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
    if (m_base) {
#ifdef Q_OS_WIN
        ::UnmapViewOfFile(m_base);
#else
        ::munmap(m_base, m_mapBytes);
#endif
        m_base = nullptr;
    }
#ifdef Q_OS_WIN
    if (m_mapping) {
        ::CloseHandle(m_mapping);
        m_mapping = nullptr;
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

#if defined(Q_OS_WIN)
    HANDLE mapping = ::OpenFileMappingW(FILE_MAP_READ, FALSE,
                                        reinterpret_cast<LPCWSTR>(shmName.utf16()));
    if (!mapping) {
        qCWarning(lcVideo) << "OpenFileMapping" << shmName << "failed:" << ::GetLastError();
        return false;
    }
    void *base = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    MEMORY_BASIC_INFORMATION mbi {};
    if (!base || !::VirtualQuery(base, &mbi, sizeof(mbi))
        || !adopt(base, mbi.RegionSize, shmName)) {
        if (base)
            ::UnmapViewOfFile(base);
        ::CloseHandle(mapping);
        return false;
    }
    m_mapping = mapping;
    return true;
#else
    const QByteArray n = shmName.toUtf8();
#ifdef Q_OS_ANDROID
    // The rings are memfds in the backend service; it hands them over.
    int fd = ringbroker_fetch(RINGBROKER_ANDROID_SOCKET, n.constData());
    if (fd < 0) {
        qCWarning(lcVideo) << "the backend did not hand over ring" << shmName;
        return false;
    }
#else
    int fd = ::shm_open(n.constData(), O_RDONLY, 0);
    if (fd < 0) {
        qCWarning(lcVideo) << "shm_open" << shmName << "failed:" << strerror(errno);
        return false;
    }
#endif
    struct stat st {};
    if (::fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < kHdrBytes) {
        ::close(fd);
        return false;
    }
    void *base = ::mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (base == MAP_FAILED)
        return false;
    if (!adopt(base, st.st_size, shmName)) {
        ::munmap(base, st.st_size);
        return false;
    }
    return true;
#endif
}

bool FrameChannel::adopt(void *base, size_t bytes, const QString &label)
{
    auto *h = static_cast<RingHeader *>(base);
    if (bytes < kHdrBytes || h->magic != kMagic || h->version != kVersion ||
        h->slotCount == 0 || h->slotBytes == 0) {
        qCWarning(lcVideo) << "not a valid frame ring:" << label;
        return false;
    }

    m_base = base;
    m_mapBytes = bytes;
    m_slotBytes = h->slotBytes;
    m_slots = h->slotCount;
    m_lastSeen = 0;
    qCDebug(lcVideo) << "opened ring" << label << h->maxWidth << "x" << h->maxHeight
                     << "slots" << h->slotCount;
    return true;
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
