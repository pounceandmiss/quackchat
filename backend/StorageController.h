// Local storage encryption, exposed to QML as `App.storage`. tacky owns it all
// (its doc/DOC.md, storage module); this carries the status across.
//
// The status is tacky's own string rather than an enum: all five values drive
// the UI, so an enum would only be a second vocabulary to keep in step.
#ifndef STORAGECONTROLLER_H
#define STORAGECONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class TackyBackend;

class StorageController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // plaintext | pending-encrypt | locked | pending-decrypt | unlocked. Empty
    // until the first reply lands, which is not "not encrypted": nothing may
    // act on it before then.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Whether the startup gate has something to do. See gateActive().
    Q_PROPERTY(bool gateActive READ gateActive NOTIFY statusChanged)
    // The status is known and nothing is gating it, so every other module is
    // installed and can be asked.
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    // A call of ours is out.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // The last failure in tacky's own words ("incorrect passphrase"), shown
    // verbatim as the Tk dialogs show it.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int progressDone READ progressDone NOTIFY progressChanged)
    Q_PROPERTY(int progressTotal READ progressTotal NOTIFY progressChanged)

public:
    explicit StorageController(QObject *parent = nullptr);

    QString status() const { return m_status; }
    bool gateActive() const;
    bool ready() const { return !m_status.isEmpty() && !gateActive(); }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    int progressDone() const { return m_done; }
    int progressTotal() const { return m_total; }

    void setBackend(TackyBackend *backend);

    // Nothing announces the status, so whoever needs it asks. Re-asked after
    // every method that can move it: unlocking can itself turn up a pending
    // decrypt, so the next state is read rather than assumed.
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void unlock(const QString &passphrase);
    Q_INVOKABLE void requestEncrypt();
    Q_INVOKABLE void requestDecrypt();
    Q_INVOKABLE void cancelPending();
    Q_INVOKABLE void encrypt(const QString &passphrase);
    Q_INVOKABLE void decrypt();

    // Public so tests can drive them with canned events and replies.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

signals:
    void statusChanged();
    void busyChanged();
    void errorChanged();
    void progressChanged();

private:
    // Whether a status is one tacky serves nothing else from.
    static bool gating(const QString &status);

    // One token for all six: the gate offers one button and so does the card,
    // so there is never a second action in flight.
    void act(const QString &method, const QVariantMap &args = {});
    void setStatus(const QString &status);
    void setBusy(bool busy);
    void setError(const QString &message);

    TackyBackend *m_backend = nullptr;
    QString m_status;
    QString m_error;
    bool m_busy = false;
    // True until the first answer that is not gating, which is what keeps the
    // gate to startup rather than something Preferences can summon.
    bool m_gateArmed = true;
    int m_done = 0;
    int m_total = 0;
    int m_statusToken = -1;
    int m_actionToken = -1;
};

#endif // STORAGECONTROLLER_H
