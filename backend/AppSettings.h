// The preferences that belong to the app rather than to an account, exposed to
// QML as `App.settings`. tacky keeps them in one global key/value store; this
// is the slice of it the GUI has anything to say about.
//
// Every value is a string and an unset key reads as an empty one, so tacky's
// own defaults are repeated here, as they are in the Tk client's setting menu
// (`variable autofetchVar "everyone"`). A control showing what is in force
// cannot show "" for a setting quietly behaving as `everyone`.
#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class TackyBackend;

class AppSettings : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // everyone | contacts | never: whose inline images load unasked.
    Q_PROPERTY(QString attachmentAutofetch READ attachmentAutofetch
                   NOTIFY attachmentAutofetchChanged)
    // And how big one may be, in bytes; 0 is no cap.
    Q_PROPERTY(qlonglong attachmentAutofetchMax READ attachmentAutofetchMax
                   NOTIFY attachmentAutofetchMaxChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString attachmentAutofetch() const { return m_autofetch; }
    qlonglong attachmentAutofetchMax() const { return m_autofetchMax; }

    void setBackend(TackyBackend *backend);

    // Nothing announces them at startup, so whoever shows them asks once.
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void setAttachmentAutofetch(const QString &policy);
    Q_INVOKABLE void setAttachmentAutofetchMax(qlonglong bytes);

    // Public so tests can drive them with canned events and replies.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

signals:
    void attachmentAutofetchChanged();
    void attachmentAutofetchMaxChanged();

private:
    void applyValue(const QString &key, const QString &value);
    void write(const QString &key, const QString &value);

    TackyBackend *m_backend = nullptr;
    // tacky's own defaults, in force until a stored value replaces them.
    QString m_autofetch = QStringLiteral("everyone");
    qlonglong m_autofetchMax = 5242880;

    int m_autofetchToken = -1;
    int m_autofetchMaxToken = -1;
};

#endif // APPSETTINGS_H
