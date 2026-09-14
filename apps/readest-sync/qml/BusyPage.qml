import QtQuick
import com.pocketbook.controls

Column {
    required property var shell
    spacing: shell.gap
    BodyText { width: parent.width; text: shell.view.status || "Working…" }
    ActionButton { width: parent.width; text: "Cancel and exit"; onAction: shell.view.close() }
}
