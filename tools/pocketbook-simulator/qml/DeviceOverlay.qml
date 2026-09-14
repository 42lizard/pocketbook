import QtQuick
Item {
    id: overlay
    objectName: "simulatorOverlay"
    anchors.fill: parent
    z: 100
    property bool panelOpen: true
    property string statusLabel: "Simulator"
    property Component appControls
    property Component appScreen
    property int networkMode: 0
    Loader { anchors.fill: parent; sourceComponent: overlay.appScreen }
    Button {
        anchors { right: parent.right; top: parent.top; margins: 12 }
        width: 280
        text: overlay.panelOpen ? "Hide simulator" : overlay.statusLabel
        onClicked: overlay.panelOpen = !overlay.panelOpen
    }
    Rectangle {
        objectName: "simulatorPanel"
        visible: overlay.panelOpen
        anchors { top: parent.top; right: parent.right; bottom: parent.bottom; margins: 24; topMargin: 90 }
        width: Math.min(820, parent.width - 48)
        color: "#edf3f8"; border.color: "#738393"; radius: 12
        MouseArea { anchors.fill: parent }
        Flickable {
            anchors { fill: parent; margins: 28 }
            contentHeight: controls.height
            clip: true
            Column {
                id: controls
                width: parent.width; spacing: 18
                Label { width: parent.width; text: "PocketBook simulator"; font.pixelSize: 38; font.bold: true }
                Label { width: parent.width; text: "Shared device simulator · no firmware" }
                Label { width: parent.width; text: "Scroll this panel for all app and device controls."; font.pixelSize: 23 }
                Loader { width: parent.width; sourceComponent: overlay.appControls }
                Label { width: parent.width; text: "Device buttons"; font.bold: true }
                Row {
                    width: parent.width; spacing: 12
                    Button { width: (parent.width - 24) / 3; text: "Previous"; onClicked: simDevice.button(Qt.Key_PageUp) }
                    Button { width: (parent.width - 24) / 3; text: "Next"; onClicked: simDevice.button(Qt.Key_PageDown) }
                    Button { width: (parent.width - 24) / 3; text: "Back"; onClicked: simDevice.button(Qt.Key_Back) }
                }
                Button { text: "Rotate portrait / landscape"; onClicked: simDevice.rotate() }
                Button { text: "Save app screenshot"; onClicked: { overlay.panelOpen = false; capture.start() } }
                Label { width: parent.width; text: "Wi-Fi connection"; font.bold: true }
                Repeater {
                    model: ["Connected", "Connection failure", "Delayed callback (5 seconds)", "Hold callback (app decides timeout)"]
                    delegate: Button {
                        required property string modelData
                        required property int index
                        text: modelData; selected: overlay.networkMode === index
                        onClicked: { overlay.networkMode = index; simDevice.network(index) }
                    }
                }
                Button { text: "Release held callback"; onClicked: simDevice.releaseNetwork() }
                Label { width: parent.width; text: simDevice.message }
                Label { width: parent.width; text: "If Vimium captures letters or navigation keys, press i before typing. Escape leaves Vimium insert mode."; font.pixelSize: 23 }
            }
        }
    }
    Timer { id: capture; interval: 150; onTriggered: simDevice.screenshot() }
}
