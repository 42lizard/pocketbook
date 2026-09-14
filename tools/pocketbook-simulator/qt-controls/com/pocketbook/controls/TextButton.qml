import QtQuick
Rectangle {
    id: button
    property string text
    signal action()
    color: !enabled ? "#eeeeee" : tap.pressed ? "black" : "white"
    border.color: "black"
    Text { anchors.centerIn: parent; text: button.text; color: tap.pressed ? "white" : "black"; font.pixelSize: 26 }
    MouseArea { id: tap; anchors.fill: parent; onClicked: button.action() }
}
