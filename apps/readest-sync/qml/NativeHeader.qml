import QtQuick
import com.pocketbook.controls

Item {
    id: root
    property string title
    property bool showBack: false
    property string actionText: ""
    signal back()
    signal action()
    signal close()

    AppHeader {
        anchors.fill: parent
        title: root.title
        onClose: root.close()
    }
    Rectangle {
        visible: root.showBack
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: Math.min(parent.width * 0.14, parent.height)
        color: backTap.pressed ? "black" : "white"
        Text {
            anchors.centerIn: parent
            text: "‹"
            color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
            font.pixelSize: Math.max(44, parent.height * 0.52)
        }
        MouseArea { id: backTap; anchors.fill: parent; onClicked: root.back() }
    }
    Rectangle {
        visible: root.actionText.length > 0
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: Math.min(parent.width * 0.2, parent.height * 1.5)
        color: actionTap.pressed ? "black" : "white"
        Text {
            anchors.centerIn: parent
            text: root.actionText
            color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
            font.pixelSize: Math.max(26, parent.height * 0.25)
        }
        MouseArea { id: actionTap; anchors.fill: parent; onClicked: root.action() }
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: "black"
    }
}
