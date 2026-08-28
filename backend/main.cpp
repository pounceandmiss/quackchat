// GUI host. The wiring all lives in the `App` singleton; this just brings the
// account online and loads the QML.
//
//   TACKY_ACC=you@example.com TACKY_PASSWORD=secret ./quackchat
//
// Without TACKY_ACC it still runs, against a persistent account-less session.
//
// --debug-file/--debug-level are for a report from a run that will not reach
// the settings page; the Diagnostics toggle covers the ordinary case.
#include <QCommandLineParser>
#ifdef Q_OS_ANDROID
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>

#include <algorithm>
#include <utility>
#endif
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>

#include "AppController.h"
#include "AvatarImageProvider.h"
#include "LogBridge.h"
#include "QImageAvatarEncoder.h"

int main(int argc, char *argv[]) {
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // A stock GNOME session exports neither of these (that is a Plasma-session
    // habit), so unset they leave Qt on its bland generic-Unix theme and the
    // Basic controls style: a hardcoded palette with no light/dark or accent
    // awareness. "xdgdesktopportal" instead asks the XDG Desktop Portal for
    // Settings, which xdg-desktop-portal-gnome answers from GNOME's own
    // preferences - unlike the "gtk3" theme (see appimage/Dockerfile, which
    // strips it for the GTK dependency it would otherwise drag in), this links
    // no GTK. Fusion is the one bundled QQC2 style that actually paints from
    // QGuiApplication::palette() rather than a style-defined one, so it is what
    // turns that palette into control colours. Set only as a default: never
    // override an environment that already chose.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
#endif
    QGuiApplication app(argc, argv);
    // The basename of icons/io.github.pounceandmiss.Quack.desktop, twice over:
    // the notifier posts it as the `desktop-entry` hint, which is how a daemon
    // finds our icon and files every popup under one app, and Qt hands
    // desktopFileName to the compositor as the window's app id, which is how a
    // taskbar matches a window to that same entry. Reverse-DNS because Flatpak
    // exports nothing whose name lacks the app id.
    app.setApplicationName(QStringLiteral("io.github.pounceandmiss.Quack"));
    app.setDesktopFileName(app.applicationName());

#ifdef Q_OS_ANDROID
    // Android splits Noto Sans Symbols over two files that share a family and a
    // style name but not one character of coverage, and Qt keys a font by family
    // and style, so whichever is read second replaces the other. /system/fonts is
    // read in name order, which leaves the 124-glyph subset registered and puts
    // the 4,600-glyph one out of reach: ✓ ★ ➤ and the mathematical alphabets a
    // room writes its subject in all come out as boxes. Adding them back, largest
    // last, settles it the other way, and costs nothing - every character in the
    // small subset is a pictograph the colour emoji font carries too.
    //
    // families() is here to populate the database first. An application font
    // added before that does not survive it, and nothing above asks the database
    // for anything.
    QFontDatabase::families();
    QFileInfoList symbolFonts =
        QDir(QStringLiteral("/system/fonts"))
            .entryInfoList({QStringLiteral("NotoSansSymbols*.ttf")}, QDir::Files);
    std::sort(symbolFonts.begin(), symbolFonts.end(),
              [](const QFileInfo &a, const QFileInfo &b) { return a.size() < b.size(); });
    for (const QFileInfo &font : std::as_const(symbolFonts))
        QFontDatabase::addApplicationFont(font.absoluteFilePath());
#endif

    // Rasters, not icons/quack.svg: the SVG would need the svg icon engine
    // plugin deployed alongside.
    QIcon icon;
    for (int size : {48, 128, 256})
        icon.addFile(QStringLiteral(":/icons/quack-%1.png").arg(size));
    QGuiApplication::setWindowIcon(icon);

    // Our own catalogues are compiled into :/i18n; Qt's cover what its dialogs
    // and text fields say for themselves, and live wherever this Qt was
    // installed. Either one missing for the current locale leaves that half in
    // the source English, which is why a failed load is not an error.
    //
    // The source-language catalogue goes on unconditionally, underneath the
    // locale's: a plural in the sources reads "%n person(s)", which is a
    // placeholder for the forms English itself needs and not something to put
    // on screen. A later translator is consulted first, so a locale that has
    // its own answer still wins.
    //
    // All three are installed before the engine reads any QML, since qsTr()
    // resolves as a binding is first evaluated and QML does not re-resolve on
    // its own.
    QTranslator sourceTranslator;
    if (sourceTranslator.load(QStringLiteral("quack_en"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&sourceTranslator);
    QTranslator appTranslator;
    if (appTranslator.load(QLocale(), QStringLiteral("quack"), QStringLiteral("_"),
                           QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&appTranslator);
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&qtTranslator);

    // parse(), not process(): Qt's own options are still in arguments() here,
    // and process() would exit over the first one it does not know.
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QGuiApplication::translate("main", "A chat client."));
    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption levelOption(
        QStringLiteral("debug-level"),
        QGuiApplication::translate(
            "main", "How much to log: verbose, debug, info, warning, error, "
                    "fatal or none."),
        QGuiApplication::translate("main", "level"));
    const QCommandLineOption fileOption(
        QStringLiteral("debug-file"),
        QGuiApplication::translate(
            "main", "Write the log here instead of where the Diagnostics "
                    "setting says."),
        QGuiApplication::translate("main", "path"));
    // The two libraries behind calls, which log at their own levels and at
    // length. Separate flags because the two can differ.
    const QCommandLineOption datachannelOption(
        QStringLiteral("libdatachannel-debug-level"),
        QGuiApplication::translate("main", "How much libdatachannel logs."),
        QGuiApplication::translate("main", "level"));
    const QCommandLineOption rtcmaOption(
        QStringLiteral("rtcma-debug-level"),
        QGuiApplication::translate("main", "How much rtc-ma logs."),
        QGuiApplication::translate("main", "level"));
    parser.addOption(levelOption);
    parser.addOption(fileOption);
    parser.addOption(datachannelOption);
    parser.addOption(rtcmaOption);
    parser.parse(QCoreApplication::arguments());
    if (parser.isSet(helpOption))
        parser.showHelp(0);

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
        controller->setDebugArgs({parser.value(levelOption),
                                  parser.value(fileOption),
                                  parser.value(datachannelOption),
                                  parser.value(rtcmaOption)});
        // Early, so the handler is in place before anything logs; it stays
        // inert until the backend answers with a file to forward to.
        installLogBridge(controller->backend());
        controller->startFromEnvironment();
    }

    engine.loadFromModule("Quack", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
