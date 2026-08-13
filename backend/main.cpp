// GUI host. The wiring all lives in the `App` singleton; this just brings the
// account online and loads the QML.
//
//   TACKY_ACC=you@example.com TACKY_PASSWORD=secret ./quackchat
//
// Without TACKY_ACC it still runs, against a persistent account-less session.
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>

#include "AppController.h"
#include "AvatarImageProvider.h"
#include "QImageAvatarEncoder.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    // What a notification daemon looks us up by (the `desktop-entry` hint), so
    // it can find our icon and file the popup under one app. Matches the
    // basename of icons/quackchat.desktop.
    app.setApplicationName(QStringLiteral("quackchat"));

    // Rasters, not icons/quack.svg: the SVG would need the svg icon engine
    // plugin deployed alongside.
    QIcon icon;
    for (int size : {48, 128, 256})
        icon.addFile(QStringLiteral(":/icons/quack-%1.png").arg(size));
    QGuiApplication::setWindowIcon(icon);

    // Declared before the engine so it outlives everything holding a pointer
    // to it.
    QImageAvatarEncoder avatarEncoder;

    QQmlApplicationEngine engine;
    // Create the singleton and connect before the UI loads, so the first frame
    // already shows the real connection state.
    if (auto *controller = engine.singletonInstance<AppController *>("Quack", "App")) {
        engine.addImageProvider(QStringLiteral("avatar"), // engine takes ownership
                                new AvatarImageProvider(controller->avatars()));
        controller->setAvatarEncoder(&avatarEncoder);
        controller->startFromEnvironment();
    }

    engine.loadFromModule("Quack", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
