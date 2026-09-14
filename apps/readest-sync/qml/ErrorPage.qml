import QtQuick
import com.pocketbook.controls

Column {
    required property var shell
    spacing: shell.gap
    BodyText { width: parent.width; text: shell.view.status || "Starting…" }
    ActionButton { width: parent.width; text: "Exit"; onAction: shell.view.close() }
}
