// Camera selection, exposed to QML as `App.video`. Mirrors AudioDevices:
// cameras belong to the machine, not an XMPP account, so tacky keeps
// them on a process-global `video` module. Setting a camera persists it
// and hot-swaps every live video call.
#ifndef CAMERADEVICES_H
#define CAMERADEVICES_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class TackyBackend;

class CameraDevices : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // Each entry is {name, id, facing}; id "" lets the backend pick.
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY camerasChanged)
    Q_PROPERTY(QString camera READ camera NOTIFY cameraChanged)

public:
    explicit CameraDevices(QObject *parent = nullptr);

    QVariantList cameras() const { return m_cameras; }
    QString camera() const { return m_camera; }

    void setBackend(TackyBackend *backend);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setCamera(const QString &id);

    // Public so tests can drive canned events / replies.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

signals:
    void camerasChanged();
    void cameraChanged();

private:
    void applyCamera(const QString &id);

    TackyBackend *m_backend = nullptr;
    QVariantList  m_cameras;
    QString       m_camera;
    int           m_camerasToken = -1;
    int           m_cameraToken = -1;
};

#endif // CAMERADEVICES_H
