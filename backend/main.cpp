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
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
#include <QFileInfo>
#include <QStandardPaths>
#endif
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QTranslator>

#include "AppController.h"
#include "AvatarImageProvider.h"
#include "LogBridge.h"
#include "QImageAvatarEncoder.h"

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
// Read off disk rather than asked over D-Bus: this has to answer before
// QGuiApplication exists, and a session bus opened that early warns about it.
static bool hasXdgPortal() {
    // Flatpak always has one, and leaves no activation file to find it by.
    if (QFileInfo::exists(QStringLiteral("/.flatpak-info")))
        return true;
    for (const QString &dir :
         QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        if (QFileInfo::exists(
                dir + QStringLiteral(
                          "/dbus-1/services/org.freedesktop.portal.Desktop.service")))
            return true;
    }
    return false;
}
#endif

int main(int argc, char *argv[]) {
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Unset, Qt reads GTK's settings through its own "gtk3" theme, but the
    // AppImage strips that plugin for the GTK dependency it drags in (see
    // appimage/Dockerfile) and falls back to a hardcoded palette with no
    // light/dark or accent awareness; the portal answers both and links no GTK.
    // Only where one is installed to answer, though: without one it wraps the
    // theme Qt would have picked anyway and fails two calls per start. A
    // default, not an override.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME") && hasXdgPortal())
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif
#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || defined(Q_OS_MACOS)
    // Fusion is the one bundled QQC2 style that actually paints from
    // QGuiApplication::palette() rather than a style-defined one, so it is what
    // turns that palette into control colours. macOS needs it for a second
    // reason: the native style refuses the control customization this UI does,
    // logging a "does not support customization" line per control. Default only,
    // as above.
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
    // The native libraries behind calls, which log at their own levels and at
    // length. Separate flags because they can differ.
    const QCommandLineOption datachannelOption(
        QStringLiteral("libdatachannel-debug-level"),
        QGuiApplication::translate("main", "How much libdatachannel logs."),
        QGuiApplication::translate("main", "level"));
    const QCommandLineOption rtcmaOption(
        QStringLiteral("rtcma-debug-level"),
        QGuiApplication::translate("main", "How much rtc-ma logs."),
        QGuiApplication::translate("main", "level"));
    const QCommandLineOption webrtcOption(
        QStringLiteral("webrtc-debug-level"),
        QGuiApplication::translate("main", "How much libwebrtc logs."),
        QGuiApplication::translate("main", "level"));
    const QCommandLineOption rtcmvOption(
        QStringLiteral("rtcmv-debug-level"),
        QGuiApplication::translate("main", "How much rtc-mv logs."),
        QGuiApplication::translate("main", "level"));
    const QCommandLineOption backendOption(
        QStringLiteral("media-backend"),
        QGuiApplication::translate(
            "main", "Media backend for calls this run: rtc or webrtc. Overrides "
                    "the setting without changing it."),
        QGuiApplication::translate("main", "name"));
    parser.addOption(levelOption);
    parser.addOption(fileOption);
    parser.addOption(datachannelOption);
    parser.addOption(rtcmaOption);
    parser.addOption(webrtcOption);
    parser.addOption(rtcmvOption);
    parser.addOption(backendOption);
    parser.parse(QCoreApplication::arguments());
    if (parser.isSet(helpOption))
        parser.showHelp(0);

    // So the frontend's own categories reach a log someone sends in. Not the
    // wire one: a line per stanza, and the bridge drops it anyway.
    // QT_LOGGING_RULES still overrides this.
    const QString debugLevel = parser.value(levelOption);
    if (debugLevel == QLatin1String("verbose") || debugLevel == QLatin1String("debug"))
        QLoggingCategory::setFilterRules(
            QStringLiteral("quack.*.debug=true\nquack.wire.debug=false"));
    else if (debugLevel == QLatin1String("info"))
        QLoggingCategory::setFilterRules(
            QStringLiteral("quack.*.info=true\nquack.wire.info=false"));

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
                                  parser.value(rtcmaOption),
                                  parser.value(webrtcOption),
                                  parser.value(rtcmvOption)});
        controller->setMediaBackendOverride(parser.value(backendOption));
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
