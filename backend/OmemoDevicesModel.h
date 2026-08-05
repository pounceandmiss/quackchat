// One subject's OMEMO device keys: a row per device with its fingerprint and
// trust state, plus the account's own fingerprint and the account-wide blind
// trust setting. Seeded from `omemo trustList` and kept live by omemo <TrustList>.
//
// Trust is three-way (trusted / untrusted / undecided). `compromised` is a
// fourth state the backend sets itself when a peer's identity key rotates; it
// is sticky and `omemo trust` refuses to move out of it, so those rows are
// read-only here.
#ifndef OMEMODEVICESMODEL_H
#define OMEMODEVICESMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class OmemoDevicesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    Q_PROPERTY(QString jid READ jid WRITE setJid NOTIFY jidChanged)
    // This device's own key. Empty until the account has connected once, since
    // the OMEMO store is built on <Ready>.
    Q_PROPERTY(QString ownFingerprint READ ownFingerprint NOTIFY ownChanged)
    Q_PROPERTY(int ownDevice READ ownDevice NOTIFY ownChanged)
    Q_PROPERTY(bool blindTrust READ blindTrust WRITE setBlindTrust NOTIFY blindTrustChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // The rows a "set all" acts on, and their shared trust state, which is ""
    // when they disagree - that is what leaves the set-all control unset.
    Q_PROPERTY(int settableCount READ settableCount NOTIFY summaryChanged)
    Q_PROPERTY(QString commonTrust READ commonTrust NOTIFY summaryChanged)

public:
    enum Role {
        DeviceRole = Qt::UserRole + 1,
        TrustRole,       // trusted, untrusted, undecided, compromised
        ActiveRole,      // still in the subject's published devicelist
        FingerprintRole, // hex, unspaced
        SettableRole,    // false for compromised rows, which cannot be moved
    };
    Q_ENUM(Role)

    explicit OmemoDevicesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TackyBackend *backend() const { return m_backend; }
    void setBackend(TackyBackend *backend);

    QString account() const { return m_account; }
    void setAccount(const QString &acc);

    QString jid() const { return m_jid; }
    void setJid(const QString &jid);

    QString ownFingerprint() const { return m_ownFingerprint; }
    int ownDevice() const { return m_ownDevice; }
    bool blindTrust() const { return m_blindTrust; }
    void setBlindTrust(bool on);

    int settableCount() const;
    QString commonTrust() const;

    // (Re)read every piece of state for the current account and subject.
    Q_INVOKABLE void refresh();

    // Fire-and-forget; the resulting <TrustList> is what updates the rows. A
    // rejected transition (compromised, or a device with no key on file yet)
    // leaves them as they were.
    Q_INVOKABLE void setTrust(int device, const QString &state);
    Q_INVOKABLE void setAllTrust(const QString &state);

    // Routing and transforms are public so tests can drive them with canned data.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);

    void applyTrustList(const QVariantList &rows);
    void applyOwnFingerprint(const QString &fingerprint);
    void applyOwnDevice(int device);
    void applyBlindTrust(bool on);

signals:
    void backendChanged();
    void accountChanged();
    void jidChanged();
    void ownChanged();
    void blindTrustChanged();
    void countChanged();
    void summaryChanged();

private:
    struct Device {
        int device = 0;
        QString trust;
        QString fingerprint;
        bool active = false;

        bool settable() const { return trust != QLatin1String("compromised"); }
    };

    // The subject is this account, so the current device is badged by the page
    // rather than listed among the rows.
    bool isOwn() const { return !m_jid.isEmpty() && m_jid == m_account; }
    // Rows minus the current device, which the page badges separately.
    QList<Device> visibleRows(const QVariantList &rows) const;
    void setRows(const QList<Device> &rows);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_jid;
    QString m_ownFingerprint;
    int m_ownDevice = 0;
    bool m_blindTrust = false;
    int m_trustToken = -1;
    int m_blindToken = -1;
    int m_fingerprintToken = -1;
    int m_deviceToken = -1;
    QVariantList m_lastRows; // as received, so a late device_id can re-filter
    QList<Device> m_rows;
};

#endif // OMEMODEVICESMODEL_H
