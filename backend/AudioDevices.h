// Mic and speaker selection plus gain, exposed to QML as `App.audio`.
//
// These belong to the machine, not to an XMPP account, so tacky keeps them on a
// process-global `audio` module and none of its events carry an `acc`. Setting a
// device or a volume persists it and hot-swaps every live call on every account,
// which is why the call UI drives sound through here rather than through the
// per-call `calls setDevices` override - the same thing tacky's own GUI does.
//
// Volume is a linear gain in [0.0, 1.0]. Mute is not a backend concept: it is
// volume 0.0 with the previous level remembered here, as in tacky's picker.
#ifndef AUDIODEVICES_H
#define AUDIODEVICES_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class TackyBackend;

class AudioDevices : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // Each entry is {name, id, default}; id "" means the system default.
    Q_PROPERTY(QVariantList captureDevices READ captureDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList playbackDevices READ playbackDevices NOTIFY devicesChanged)
    Q_PROPERTY(QString captureDevice READ captureDevice NOTIFY captureDeviceChanged)
    Q_PROPERTY(QString playbackDevice READ playbackDevice NOTIFY playbackDeviceChanged)
    Q_PROPERTY(qreal captureVolume READ captureVolume NOTIFY captureVolumeChanged)
    Q_PROPERTY(qreal playbackVolume READ playbackVolume NOTIFY playbackVolumeChanged)
    Q_PROPERTY(bool captureMuted READ captureMuted NOTIFY captureVolumeChanged)
    Q_PROPERTY(bool playbackMuted READ playbackMuted NOTIFY playbackVolumeChanged)

public:
    explicit AudioDevices(QObject *parent = nullptr);

    QVariantList captureDevices() const { return m_captureDevices; }
    QVariantList playbackDevices() const { return m_playbackDevices; }
    QString captureDevice() const { return m_captureDevice; }
    QString playbackDevice() const { return m_playbackDevice; }
    qreal captureVolume() const { return m_captureVolume; }
    qreal playbackVolume() const { return m_playbackVolume; }
    bool captureMuted() const { return m_captureVolume <= 0.0; }
    bool playbackMuted() const { return m_playbackVolume <= 0.0; }

    void setBackend(TackyBackend *backend);

    // Re-read devices, preferences and volumes. Devices are not hot-plug
    // watched by the backend, so a picker that wants a fresh list asks again.
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void setCaptureDevice(const QString &id);
    Q_INVOKABLE void setPlaybackDevice(const QString &id);
    Q_INVOKABLE void setCaptureVolume(qreal volume);
    Q_INVOKABLE void setPlaybackVolume(qreal volume);
    // Drop to 0.0, or back to the level from before the mute.
    Q_INVOKABLE void setCaptureMuted(bool muted);
    Q_INVOKABLE void setPlaybackMuted(bool muted);

    // Public so tests can drive it with canned events and replies.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

signals:
    void devicesChanged();
    void captureDeviceChanged();
    void playbackDeviceChanged();
    void captureVolumeChanged();
    void playbackVolumeChanged();

private:
    void applyDevice(const QString &kind, const QString &id);
    void applyVolume(const QString &kind, qreal volume);
    void setVolume(const QString &kind, qreal volume);

    TackyBackend *m_backend = nullptr;
    QVariantList m_captureDevices;
    QVariantList m_playbackDevices;
    QString m_captureDevice;
    QString m_playbackDevice;
    // 1.0 (unity) is what the backend returns for an unset gain.
    qreal m_captureVolume = 1.0;
    qreal m_playbackVolume = 1.0;
    qreal m_captureUnmuted = 1.0;
    qreal m_playbackUnmuted = 1.0;

    int m_devicesToken = -1;
    int m_captureDeviceToken = -1;
    int m_playbackDeviceToken = -1;
    int m_captureVolumeToken = -1;
    int m_playbackVolumeToken = -1;
};

#endif // AUDIODEVICES_H
