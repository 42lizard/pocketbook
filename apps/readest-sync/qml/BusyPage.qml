import QtQuick

Rectangle {
    required property var shell
    color: "white"
    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * shell.metrics.largeGap, 1200 * shell.metrics.scale)
        Text {
            width: parent.width
            text: shell.view.status || "Starting…"
            font.pixelSize: shell.metrics.titleFont
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        Rectangle {
            width: parent.width
            height: shell.metrics.primaryAction
            color: cancelTap.pressed ? "black" : "white"
            border.color: "black"
            Text {
                anchors.fill: parent
                text: "Cancel and exit"
            color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                font.pixelSize: shell.metrics.bodyFont
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea { id: cancelTap; anchors.fill: parent; onClicked: shell.view.close() }
        }
    }
}
