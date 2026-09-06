// The preferences that belong to the app rather than to an account, exposed to
// QML as `App.settings`. tacky keeps them in one global key/value store; this
// is the slice of it the GUI has anything to say about.
//
// Every value is a string and an unset key reads as an empty one, so tacky's
// own defaults are repeated here, as they are in the Tk client's setting menu
// (`variable autofetchVar "contacts"`). A control showing what is in force
// cannot show "" for a setting quietly behaving as `contacts`. They are tacky's
// to change, so each one names where it comes from: this is `file.tcl`'s
// AUTOFETCH_DEFAULT.
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
    // Whether the backend writes its log to a file. The same key the Tk
    // client's File menu toggles, so the two agree about one session's store.
    Q_PROPERTY(bool logToFile READ logToFile NOTIFY logToFileChanged)
    // How much of it: verbose, debug, info, warning, error or none.
    Q_PROPERTY(QString logLevel READ logLevel NOTIFY logLevelChanged)
    // The native loggers inside libdatachannel and rtc-ma. They filter their
    // own output and are voluminous with it, so this is a switch rather than a
    // level of its own.
    Q_PROPERTY(bool logNative READ logNative NOTIFY logNativeChanged)
    // Whether the chat feed draws a face beside each run of messages. A key of
    // our own: the Tk client draws them either way.
    Q_PROPERTY(bool chatAvatars READ chatAvatars NOTIFY chatAvatarsChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString attachmentAutofetch() const { return m_autofetch; }
    qlonglong attachmentAutofetchMax() const { return m_autofetchMax; }
    bool logToFile() const { return m_logToFile; }
    QString logLevel() const { return m_logLevel; }
    bool logNative() const { return m_logNative; }
    bool chatAvatars() const { return m_chatAvatars; }

    void setBackend(TackyBackend *backend);

    // Nothing announces them at startup, so whoever shows them asks once.
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void setAttachmentAutofetch(const QString &policy);
    Q_INVOKABLE void setAttachmentAutofetchMax(qlonglong bytes);
    Q_INVOKABLE void setLogToFile(bool on);
    Q_INVOKABLE void setLogLevel(const QString &level);
    Q_INVOKABLE void setLogNative(bool on);
    Q_INVOKABLE void setChatAvatars(bool on);

    // Public so tests can drive them with canned events and replies.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

signals:
    void attachmentAutofetchChanged();
    void attachmentAutofetchMaxChanged();
    void logToFileChanged();
    void logLevelChanged();
    void logNativeChanged();
    void chatAvatarsChanged();

private:
    void applyValue(const QString &key, const QString &value);
    void write(const QString &key, const QString &value);

    TackyBackend *m_backend = nullptr;
    // tacky's own defaults, in force until a stored value replaces them.
    QString m_autofetch = QStringLiteral("contacts");
    qlonglong m_autofetchMax = 5242880;
    bool m_logToFile = false;
    QString m_logLevel = QStringLiteral("warning");
    bool m_logNative = false;
    bool m_chatAvatars = true;

    int m_autofetchToken = -1;
    int m_autofetchMaxToken = -1;
    int m_logToFileToken = -1;
    int m_logLevelToken = -1;
    int m_logNativeToken = -1;
    int m_chatAvatarsToken = -1;
};

#endif // APPSETTINGS_H
