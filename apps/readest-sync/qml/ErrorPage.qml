import QtQuick

Rectangle {
    required property var shell
    color: "white"
    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * shell.metrics.largeGap, 1200 * shell.metrics.scale)
        spacing: shell.metrics.largeGap
        Text {
            width: parent.width
            text: shell.view.status || "Readest Sync could not start."
            font.pixelSize: shell.metrics.bodyFont
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Rectangle {
            width: parent.width
            height: shell.metrics.primaryAction
            color: exitTap.pressed ? "black" : "white"
            border.color: "black"
            Text {
                anchors.fill: parent
                text: "Exit"
            color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                font.pixelSize: shell.metrics.bodyFont
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea { id: exitTap; anchors.fill: parent; onClicked: shell.view.close() }
        }
    }
}
