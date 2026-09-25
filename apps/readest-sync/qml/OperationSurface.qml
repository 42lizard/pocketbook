import QtQuick

Item {
    id: root
    objectName: "nativeOperationSurface"
    required property var shell
    z: 50
    Rectangle { anchors.fill: parent; color: "#dddddd"; opacity: 0.92 }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * shell.metrics.largeGap, 1200 * shell.metrics.scale)
        height: operationColumn.height
        color: "white"
        border { color: "black"; width: Math.max(2, 10 * shell.metrics.scale) }
        Column {
            id: operationColumn
            width: parent.width
            Text {
                width: parent.width
                height: shell.metrics.primaryAction
                text: shell.view.status || "Working…"
                color: "black"
                font.pixelSize: shell.metrics.bodyFont
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
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
}
