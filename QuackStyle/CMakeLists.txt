# The app's own Quick Controls style: the controls it defines are drawn in the
# app's look everywhere, and the rest fall back to the platform's style (Fusion
# on desktop, Material on Android - see main.cpp).
qt_add_library(quackstyle STATIC)
if(NOT QUACK_QML_AOT)
    set_target_properties(quackstyle PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")
endif()
qt_add_qml_module(quackstyle
    URI QuackStyle
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/QuackStyle"
    QML_FILES
        Button.qml
        DialogButtonBox.qml)
target_link_libraries(quackstyle PRIVATE Qt6::Quick Qt6::QuickTemplates2 Qt6::QuickControls2Impl)
