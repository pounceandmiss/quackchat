// GUI host. The wiring all lives in the `App` singleton; this just brings the
// account online and loads the QML.
//
//   TACKY_ACC=you@example.com TACKY_PASSWORD=secret ./quackchat
//
// Without TACKY_ACC it still runs, against a persistent account-less session.
#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "AppController.h"
#include "AvatarImageProvider.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    // What a notification daemon looks us up by (the `desktop-entry` hint), so
    // it can find our icon and file the popup under one app. Until an
    // installed quackchat.desktop exists there is nothing to find, and the
    // alert simply shows without an icon.
    app.setApplicationName(QStringLiteral("quackchat"));

    QQmlApplicationEngine engine;
    // Create the singleton and connect before the UI loads, so the first frame
    // already shows the real connection state.
    if (auto *controller = engine.singletonInstance<AppController *>("Quack", "App")) {
        engine.addImageProvider(QStringLiteral("avatar"), // engine takes ownership
                                new AvatarImageProvider(controller->avatars()));
        controller->startFromEnvironment();
    }

    engine.loadFromModule("Quack", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
