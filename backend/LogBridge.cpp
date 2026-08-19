#include "LogBridge.h"

#include <QMetaObject>
#include <QString>

#include "TackyBackend.h"

namespace {

// Set once from main() before any other thread is logging, and never cleared:
// the backend belongs to the App singleton, which outlives everything that
// could still be writing a message.
TackyBackend *g_backend = nullptr;
QtMessageHandler g_previous = nullptr;

// tacky's own level names. `none` is a threshold rather than a severity, so
// nothing maps to it.
QString levelFor(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("error");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("warning");
}

void handler(QtMsgType type, const QMessageLogContext &ctx,
             const QString &msg) {
    // Whatever was installed before us, first: a fatal message never comes back
    // from it, and the queued write below would not have run anyway.
    if (g_previous)
        g_previous(type, ctx, msg);
    if (!g_backend)
        return;
    const QVariantMap args = logWriteArgs(type, ctx, msg);
    if (args.isEmpty())
        return;
    // Queued from every thread, our own included: the backend belongs to the
    // main thread, and one queue keeps the file in the order things happened.
    QMetaObject::invokeMethod(g_backend, "notify", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("log")),
                              Q_ARG(QString, QStringLiteral("write")),
                              Q_ARG(QVariant, args));
}

} // namespace

QVariantMap logWriteArgs(QtMsgType type, const QMessageLogContext &ctx,
                         const QString &msg) {
    const QLatin1String category(ctx.category ? ctx.category : "");
    // The wire category logs each frame we send, so forwarding it would send a
    // frame per frame.
    if (category == QLatin1String("quack.wire"))
        return {};
    // Under one root, so `log setlevel {obj: "frontend"}` moves the lot and the
    // backend's own objects keep their own levels.
    QString obj = QStringLiteral("frontend");
    if (!category.isEmpty() && category != QLatin1String("default"))
        obj += QLatin1Char('.') + category;
    return QVariantMap{{QStringLiteral("level"), levelFor(type)},
                       {QStringLiteral("text"), msg},
                       {QStringLiteral("obj"), obj}};
}

void installLogBridge(TackyBackend *backend) {
    g_backend = backend;
    g_previous = qInstallMessageHandler(handler);
}
