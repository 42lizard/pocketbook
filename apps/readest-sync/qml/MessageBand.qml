import QtQuick

Rectangle {
    id: root
    objectName: "nativeMessageBand"
    required property var shell
    property string actionText: ""
    signal action()
    visible: !shell.view.busy && (shell.view.status || "").length > 0
    height: visible ? Math.max(shell.metrics.menuRow, message.implicitHeight + 2 * shell.metrics.smallGap) : 0
    color: "white"
    border { color: "black"; width: shell.view.statusKind === "warning" ? Math.max(2, 4 * shell.metrics.scale) : 1 }
    Text {
        id: message
        anchors { left: parent.left; right: actionButton.left; verticalCenter: parent.verticalCenter; margins: shell.metrics.gap }
        text: shell.view.status || ""
        color: "black"
        font.pixelSize: shell.metrics.smallFont
        wrapMode: Text.Wrap
    }
    Rectangle {
        id: actionButton
        visible: root.actionText.length > 0
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: visible ? Math.min(parent.width * 0.32, 360 * shell.metrics.scale) : 0
        color: actionTap.pressed ? "black" : "white"
        border.color: "black"
        Text {
            anchors { fill: parent; margins: shell.metrics.smallGap }
            text: root.actionText
            color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
            font.pixelSize: shell.metrics.smallFont
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
        MouseArea { id: actionTap; anchors.fill: parent; onClicked: root.action() }
    }
}
