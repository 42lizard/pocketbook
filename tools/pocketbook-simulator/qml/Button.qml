import QtQuick
Rectangle {
    id: button
    property string text
    property bool selected: false
    signal clicked()
    width: parent.width
    height: 62
    radius: 6
    color: selected ? "#17212c" : tap.pressed ? "#d3e2ee" : "white"
    border.color: "#738393"
    Text { anchors.centerIn: parent; text: button.text; font.pixelSize: 25; color: button.selected ? "white" : "#17212c" }
    MouseArea { id: tap; anchors.fill: parent; onClicked: button.clicked() }
}
