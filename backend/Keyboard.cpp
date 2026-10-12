#include "Keyboard.h"

#include <QCoreApplication>
#include <QList>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qnativeinterface.h>
#endif

// Every live instance: one per QML engine, which on a phone is one.
static QList<Keyboard *> s_instances;

#ifdef Q_OS_ANDROID
// KeyboardInsets.heightChanged, called on Android's UI thread. The value is
// handed to the Qt thread rather than set from here.
static void heightChanged(JNIEnv *, jclass, jfloat height) {
    QMetaObject::invokeMethod(qApp, [height] {
        for (Keyboard *keyboard : std::as_const(s_instances))
            keyboard->setHeight(height);
    }, Qt::QueuedConnection);
}

static void installListener() {
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    static const char *cls = "org/qtproject/example/quackchat/KeyboardInsets";
    const JNINativeMethod methods[] = {
        {"heightChanged", "(F)V", reinterpret_cast<void *>(heightChanged)},
    };
    QJniEnvironment env;
    if (!env.registerNativeMethods(cls, methods, 1))
        return;
    QJniObject::callStaticMethod<void>(
            cls, "install", "(Landroid/content/Context;)V",
            QNativeInterface::QAndroidApplication::context().object());
}
#endif

Keyboard::Keyboard(QObject *parent) : QObject(parent) {
    s_instances << this;
#ifdef Q_OS_ANDROID
    installListener();
#endif
}

Keyboard::~Keyboard() { s_instances.removeOne(this); }

void Keyboard::setHeight(qreal height) {
    height = qMax(qreal(0), height);
    if (qFuzzyCompare(height + 1, m_height + 1))
        return;
    m_height = height;
    emit heightChanged();
}
