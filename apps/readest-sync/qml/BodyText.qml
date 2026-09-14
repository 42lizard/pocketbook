import QtQuick
import QtQuick.Window
Text {
    color: "black"
    font.pixelSize: Math.max(18, (Window.window ? Window.window.width : 1404) / 43)
    wrapMode: Text.Wrap
}
