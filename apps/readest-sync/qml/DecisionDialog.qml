import QtQuick

Item {
    id: root
    objectName: "nativeDecisionDialog"
    required property var shell
    z: 60
    readonly property var choices: shell.view.actionPresentation ? shell.view.actionPresentation.choices : []

    Rectangle { anchors.fill: parent; color: "#dddddd"; opacity: 0.92 }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * shell.metrics.largeGap, 1200 * shell.metrics.scale)
        height: dialogColumn.height
        color: "white"
        border { color: "black"; width: Math.max(2, 10 * shell.metrics.scale) }
        Column {
            id: dialogColumn
            width: parent.width
            Text {
                width: parent.width
                leftPadding: shell.metrics.largeGap
                rightPadding: shell.metrics.largeGap
                topPadding: shell.metrics.largeGap
                bottomPadding: shell.metrics.largeGap
                text: shell.view.blocking ? shell.view.blockingMessage : "Choose which reading position to use."
                color: "black"
                font.pixelSize: shell.metrics.bodyFont
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            Repeater {
                model: root.choices
                delegate: Rectangle {
                    required property var modelData
                    width: dialogColumn.width
                    height: shell.metrics.primaryAction
                    color: choiceTap.pressed ? "black" : "white"
                    border.color: "black"
                    Text {
                        anchors { fill: parent; margins: shell.metrics.smallGap }
                        text: modelData.text
            color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                        font.pixelSize: shell.metrics.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.Wrap
                    }
                    MouseArea { id: choiceTap; anchors.fill: parent; onClicked: shell.view.runAction(modelData.command) }
                }
            }
            Rectangle {
                width: parent.width
                height: shell.metrics.primaryAction
                color: finalTap.pressed ? "black" : "white"
                border.color: "black"
                Text {
                    anchors.fill: parent
                    text: shell.view.blocking ? "Acknowledge" : "Cancel"
            color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                    font.pixelSize: shell.metrics.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                MouseArea {
                    id: finalTap
                    anchors.fill: parent
                    onClicked: shell.view.blocking ? shell.view.acknowledgeBlocking() : shell.view.cancelDecision()
                }
            }
        }
    }
}
