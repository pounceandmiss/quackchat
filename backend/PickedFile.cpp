#include "PickedFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QCoreApplication> // QNativeInterface::QAndroidApplication
#include <QJniObject>
#endif

namespace {

#ifdef Q_OS_ANDROID
QString askProvider(const QUrl &picked, const char *method) {
    const auto context = QNativeInterface::QAndroidApplication::context();
    const QJniObject uri = QJniObject::fromString(picked.toString());
    const QJniObject answer = QJniObject::callStaticObjectMethod(
        "org/qtproject/example/quackchat/PickedDocument", method,
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        context.object(), uri.object<jstring>());
    return answer.isValid() ? answer.toString() : QString();
}
#endif

// Empty off Android, where a file:// url is never copied and nothing asks.
QString documentName(const QUrl &picked) {
#ifdef Q_OS_ANDROID
    return askProvider(picked, "displayName");
#else
    Q_UNUSED(picked);
    return {};
#endif
}

QString documentMime(const QUrl &picked) {
#ifdef Q_OS_ANDROID
    return askProvider(picked, "mimeType");
#else
    Q_UNUSED(picked);
    return {};
#endif
}

} // namespace

namespace pickedfile {

QString nameFor(const QUrl &picked, const QString &displayName,
                const QString &mime) {
    // fileName() drops any directory the provider put in front of the name.
    QString name = QFileInfo(displayName).fileName();
    if (name.isEmpty())
        name = QFileInfo(picked.fileName()).fileName();
    // The name reaches the upload slot request and the url it answers with.
    static const QRegularExpression unsafe(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])"));
    name.replace(unsafe, QStringLiteral("_"));
    while (name.startsWith(QLatin1Char('.'))) // or the copy is a hidden file
        name.remove(0, 1);
    if (name.isEmpty())
        name = QStringLiteral("attachment");
    if (QFileInfo(name).suffix().isEmpty() && !mime.isEmpty()) {
        const QString suffix =
            QMimeDatabase().mimeTypeForName(mime).preferredSuffix();
        if (!suffix.isEmpty())
            name += QLatin1Char('.') + suffix;
    }
    return name;
}

QString localPath(const QUrl &picked) {
    if (picked.isLocalFile())
        return picked.toLocalFile();
    if (picked.scheme().isEmpty())
        return picked.path(); // a bare path, as a fixture would pass
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/outgoing");
    if (!QDir().mkpath(dir))
        return {};
    const QString dest = dir + QLatin1Char('/') +
                         nameFor(picked, documentName(picked), documentMime(picked));
    QFile::remove(dest);
    // toString(): Qt's file engine takes a content:// url as it stands.
    return QFile::copy(picked.toString(), dest) ? dest : QString();
}

} // namespace pickedfile
