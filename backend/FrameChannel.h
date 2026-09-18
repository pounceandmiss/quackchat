// Reader for an rtc-mv shared-memory frame ring, opened by the name a
// calls <VideoTrack>/<VideoPreview> event carries (on Android, through
// the backend service's ringbroker); maps it read-only and
// hands out the newest complete I420 frame. Layout mirrors
// rtc-mv/src/rtcmv_internal.h - keep in sync.

#pragma once

#include <QByteArray>
#include <QLoggingCategory>
#include <QString>
#include <cstdint>

Q_DECLARE_LOGGING_CATEGORY(lcVideo)

class FrameChannel {
public:
    struct Frame {
        quint64  seq = 0;
        qint64   ptsNs = 0;
        int      width = 0;
        int      height = 0;
        bool     keyframe = false;
        // Tightly packed I420: Y (w*h), U (w/2*h/2), V (w/2*h/2).
        QByteArray planes;
    };

    FrameChannel() = default;
    ~FrameChannel();
    FrameChannel(const FrameChannel &) = delete;
    FrameChannel &operator=(const FrameChannel &) = delete;

    // Open the ring named in the event (e.g. "/tv-abc123..."). Returns
    // false if it can't be mapped or the header is not a valid ring.
    bool openByName(const QString &shmName);
    void close();
    bool isOpen() const { return m_base != nullptr; }

    // Non-blocking. Fills `out` and returns true when a newer complete
    // frame is available; false if nothing new or a torn read (retry).
    bool read(Frame &out);

private:
    bool adopt(void *base, size_t bytes, const QString &label);
#ifdef Q_OS_WIN
    void   *m_mapping = nullptr;
#endif
    void   *m_base = nullptr;
    size_t  m_mapBytes = 0;
    size_t  m_slotBytes = 0;
    uint32_t m_slots = 0;
    quint64 m_lastSeen = 0;
};
