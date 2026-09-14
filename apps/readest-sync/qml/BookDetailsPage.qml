import QtQuick
import com.pocketbook.controls

Flickable {
    required property var shell
    contentHeight: detailColumn.height
    clip: true
    Column {
        id: detailColumn
        width: parent.width
        spacing: shell.gap
        BodyText { width: parent.width; text: shell.view.hint || "" }
        BodyText { width: parent.width; text: shell.view.status || "" }
        Repeater {
            model: shell.view.actions || []
            delegate: ActionButton {
                required property var modelData
                width: detailColumn.width
                text: modelData.text
                onAction: shell.view.runAction(modelData.command)
            }
        }
    }
}
