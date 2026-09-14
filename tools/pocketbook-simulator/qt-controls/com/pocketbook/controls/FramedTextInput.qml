import QtQuick
TextInput {
    id: input
    property string placeholderText
    font.pixelSize: 26
    color: "black"
    verticalAlignment: TextInput.AlignVCenter
    leftPadding: 12
    Text { anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
 text: input.placeholderText; visible: !input.text && !input.activeFocus; color: "#666666"; font: input.font }
    Rectangle { anchors.fill: parent; color: "transparent"; border.color: "black"; z: -1 }
}
