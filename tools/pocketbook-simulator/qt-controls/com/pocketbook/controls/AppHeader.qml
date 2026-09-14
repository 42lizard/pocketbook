// Mirrors the inspected U743g.6.11.1683 header: title and close, no canBack/back.
import QtQuick
Item {
    property string title
    signal close()
    height: 90
    Text { anchors.centerIn: parent; text: parent.title; font.pixelSize: 32; color: "black" }
    Text { anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 20 }
 text: "×"; font.pixelSize: 40; MouseArea { anchors.fill: parent; onClicked: parent.parent.close() } }
}
