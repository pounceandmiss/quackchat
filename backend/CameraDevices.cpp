#include "CameraDevices.h"

#include "BackendBinding.h"
#include "TackyBackend.h"

CameraDevices::CameraDevices(QObject *parent) : QObject(parent) {}

void CameraDevices::setBackend(TackyBackend *backend)
{
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &CameraDevices::refresh);
}

void CameraDevices::refresh()
{
    if (!m_backend)
        return;
    m_camerasToken = m_backend->request(QStringLiteral("video"),
                                        QStringLiteral("enumerateCameras"));
    m_cameraToken = m_backend->request(QStringLiteral("video"),
                                       QStringLiteral("getPreferredCamera"));
}

void CameraDevices::setCamera(const QString &id)
{
    if (!m_backend)
        return;
    m_backend->notify(QStringLiteral("video"), QStringLiteral("setPreferredCamera"),
                      QVariantMap{{QStringLiteral("id"), id}});
    applyCamera(id); // optimistic
}

void CameraDevices::handleResult(int token, const QVariant &data)
{
    if (token == m_camerasToken) {
        m_cameras = data.toList();
        emit camerasChanged();
    } else if (token == m_cameraToken) {
        applyCamera(data.toString());
    }
}

void CameraDevices::handleError(int token, const QString &message)
{
    Q_UNUSED(message)
    if (token == m_camerasToken) m_camerasToken = -1;
    if (token == m_cameraToken) m_cameraToken = -1;
}

void CameraDevices::handleEvent(const QString &module, const QString &name,
                                const QVariant &args)
{
    if (module != QLatin1String("video"))
        return;
    if (name == QLatin1String("<PreferredCamera>"))
        applyCamera(args.toMap().value(QStringLiteral("id")).toString());
}

void CameraDevices::applyCamera(const QString &id)
{
    if (m_camera == id)
        return;
    m_camera = id;
    emit cameraChanged();
}
