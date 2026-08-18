#include "AudioDevices.h"

#include "BackendBinding.h"

#include "TackyBackend.h"

namespace {
const QLatin1String kCapture("capture");
const QLatin1String kPlayback("playback");
} // namespace

AudioDevices::AudioDevices(QObject *parent) : QObject(parent) {}

void AudioDevices::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &AudioDevices::refresh);
}

void AudioDevices::refresh() {
    if (!m_backend)
        return;
    m_devicesToken = m_backend->request(QStringLiteral("audio"),
                                        QStringLiteral("enumerateDevices"));
    m_captureDeviceToken = m_backend->request(
        QStringLiteral("audio"), QStringLiteral("getPreferredDevice"),
        QVariantMap{{QStringLiteral("kind"), kCapture}});
    m_playbackDeviceToken = m_backend->request(
        QStringLiteral("audio"), QStringLiteral("getPreferredDevice"),
        QVariantMap{{QStringLiteral("kind"), kPlayback}});
    m_captureVolumeToken = m_backend->request(
        QStringLiteral("audio"), QStringLiteral("getVolume"),
        QVariantMap{{QStringLiteral("kind"), kCapture}});
    m_playbackVolumeToken = m_backend->request(
        QStringLiteral("audio"), QStringLiteral("getVolume"),
        QVariantMap{{QStringLiteral("kind"), kPlayback}});
}

void AudioDevices::handleResult(int token, const QVariant &data) {
    if (token == m_devicesToken) {
        const QVariantMap m = data.toMap();
        m_captureDevices = m.value(kCapture).toList();
        m_playbackDevices = m.value(kPlayback).toList();
        emit devicesChanged();
    } else if (token == m_captureDeviceToken) {
        applyDevice(kCapture, data.toString());
    } else if (token == m_playbackDeviceToken) {
        applyDevice(kPlayback, data.toString());
    } else if (token == m_captureVolumeToken) {
        applyVolume(kCapture, data.toDouble());
    } else if (token == m_playbackVolumeToken) {
        applyVolume(kPlayback, data.toDouble());
    }
}

// A device list or a stored preference that never came. The pickers keep what
// they have rather than emptying, and the connected edge asks again.
void AudioDevices::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    for (int *t : {&m_devicesToken, &m_captureDeviceToken, &m_playbackDeviceToken,
                   &m_captureVolumeToken, &m_playbackVolumeToken}) {
        if (*t == token) {
            *t = -1;
            return;
        }
    }
}

void AudioDevices::handleEvent(const QString &module, const QString &name,
                               const QVariant &args) {
    if (module != QLatin1String("audio"))
        return;
    const QVariantMap a = args.toMap();
    const QString kind = a.value(QStringLiteral("kind")).toString();
    // No acc to filter on - these are process-global.
    if (name == QLatin1String("PreferredDevice"))
        applyDevice(kind, a.value(QStringLiteral("id")).toString());
    else if (name == QLatin1String("Volume"))
        applyVolume(kind, a.value(QStringLiteral("volume")).toDouble());
}

void AudioDevices::applyDevice(const QString &kind, const QString &id) {
    if (kind == kCapture) {
        if (m_captureDevice == id)
            return;
        m_captureDevice = id;
        emit captureDeviceChanged();
    } else if (kind == kPlayback) {
        if (m_playbackDevice == id)
            return;
        m_playbackDevice = id;
        emit playbackDeviceChanged();
    }
}

void AudioDevices::applyVolume(const QString &kind, qreal volume) {
    if (kind == kCapture) {
        if (qFuzzyCompare(m_captureVolume, volume))
            return;
        m_captureVolume = volume;
        // Remember where to come back to, so an unmute does not land on unity
        // when the user was running quiet.
        if (volume > 0.0)
            m_captureUnmuted = volume;
        emit captureVolumeChanged();
    } else if (kind == kPlayback) {
        if (qFuzzyCompare(m_playbackVolume, volume))
            return;
        m_playbackVolume = volume;
        if (volume > 0.0)
            m_playbackUnmuted = volume;
        emit playbackVolumeChanged();
    }
}

void AudioDevices::setCaptureDevice(const QString &id) {
    if (!m_backend)
        return;
    m_backend->notify(QStringLiteral("audio"), QStringLiteral("setPreferredDevice"),
                      QVariantMap{{QStringLiteral("kind"), kCapture},
                                  {QStringLiteral("id"), id}});
    applyDevice(kCapture, id); // <PreferredDevice> confirms; don't wait to redraw
}

void AudioDevices::setPlaybackDevice(const QString &id) {
    if (!m_backend)
        return;
    m_backend->notify(QStringLiteral("audio"), QStringLiteral("setPreferredDevice"),
                      QVariantMap{{QStringLiteral("kind"), kPlayback},
                                  {QStringLiteral("id"), id}});
    applyDevice(kPlayback, id);
}

void AudioDevices::setVolume(const QString &kind, qreal volume) {
    if (!m_backend)
        return;
    volume = qBound(0.0, volume, 1.0);
    m_backend->notify(QStringLiteral("audio"), QStringLiteral("setVolume"),
                      QVariantMap{{QStringLiteral("kind"), kind},
                                  {QStringLiteral("volume"), volume}});
    applyVolume(kind, volume);
}

void AudioDevices::setCaptureVolume(qreal volume) { setVolume(kCapture, volume); }
void AudioDevices::setPlaybackVolume(qreal volume) { setVolume(kPlayback, volume); }

void AudioDevices::setCaptureMuted(bool muted) {
    setVolume(kCapture, muted ? 0.0 : m_captureUnmuted);
}

void AudioDevices::setPlaybackMuted(bool muted) {
    setVolume(kPlayback, muted ? 0.0 : m_playbackUnmuted);
}
