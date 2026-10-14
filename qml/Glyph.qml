import QtQuick
import QtQuick.Shapes
import Quack

// One path from Icons, filled in a theme colour. This is how the app draws
// every symbol that is not a real emoji: sharp at any size, and in the colour
// it is given, neither of which a font glyph on Android manages.
Item {
    id: glyph

    property string path
    property color color: Theme.textPrimary
    // Icons are square, so one number sets both sides. Material's shapes fill
    // more of their box than a font's do, so this reads heavier than the same
    // number as a pixelSize would.
    property real size: 20

    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: glyph.color
            strokeWidth: -1
            // SVG fills by winding; QML would otherwise punch a hole wherever
            // an icon's subpaths overlap (the clock face, the pencil).
            fillRule: ShapePath.WindingFill
            // The 24x24 grid the paths are drawn on, mapped onto our own size.
            scale: Qt.size(glyph.width / 24, glyph.height / 24)
            PathSvg { path: glyph.path }
        }
    }
}
