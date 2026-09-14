import QtQuick
import QtQuick.Window
import com.pocketbook.controls
Window {
    width: screenWidth
    height: screenHeight
    visible: true
    color: "white"
    title: "Hello PocketBook"
    AppHeader { width: parent.width; title: "Hello PocketBook"; onClose: Qt.quit() }
    Column {
        anchors.centerIn: parent
        width: parent.width * 0.8
        spacing: 30
        StyledText { width: parent.width; text: "Hello from Qt6!"; horizontalAlignment: Text.AlignHCenter; styledFont: FontStyles.Heading2 }
        TextButton { width: parent.width; height: 80; text: "Exit"; onAction: Qt.quit() }
    }
    Item { focus: true; Keys.onPressed: function(event) { if(event.key === Qt.Key_Back || event.key === Qt.Key_Escape || event.key === Qt.Key_Home) Qt.quit() } }
}
