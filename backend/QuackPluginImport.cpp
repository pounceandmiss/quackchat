// The Quack module's static plugin, which registers its types (App and the
// rest). qmlimportscanner pulls it into an executable at configure time; the
// tests skip that scan (see CMakeLists.txt), so they link it through this.
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(QuackPlugin)
