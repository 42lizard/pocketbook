import QtQuick
import "qrc:/pocketbook-simulator"

DeviceOverlay {
    id: overlay
    property int transferMode: 0
    statusLabel: simulator.realCloud ? "Simulator · Real" : "Simulator · Mock"
    appScreen: Component {
        Rectangle {
            id: reader
            objectName: "simulatedReader"
            anchors.fill: parent
            color: "#faf8ef"
            visible: simulator.readerPath.length > 0
            onVisibleChanged: if (visible) forceActiveFocus()
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                    simulator.closeReader(); event.accepted = true
                } else if (event.key === Qt.Key_Right || event.key === Qt.Key_PageDown) {
                    simulator.turnReader(1); event.accepted = true
                } else if (event.key === Qt.Key_Left || event.key === Qt.Key_PageUp) {
                    simulator.turnReader(-1); event.accepted = true
                }
            }
            MouseArea { anchors.fill: parent }
            Column {
                anchors { fill: parent; margins: 70; topMargin: 130 }
                spacing: 45
                Label { width: parent.width; text: "Simulated PocketBook reader"; font.pixelSize: 46; font.bold: true }
                Label { width: parent.width; text: simulator.realCloud ? "Downloaded EPUB" : "Chapter " + simulator.chapter + " / 3 · " + ["ALPHA", "BRAVO", "CHARLIE"][simulator.chapter - 1]; font.pixelSize: 38 }
                Label {
                    width: parent.width
                    text: simulator.realCloud ? "Downloaded to:\n" + simulator.readerPath + "\n\nReader handoff works, but this panel does not render EPUBs. Chapter editing is available for mock books only; it does not invent reading positions for your real library." : "This panel models reader handoff and saved positions. Turning a page writes a chapter CFI and page counts to the simulator’s reading database.\n\nReturn to Readest Sync to inspect or synchronize the changed position.\n\nIt does not render the EPUB or reproduce native pagination."
                    font.pixelSize: 32
                }
                Row {
                    visible: !simulator.realCloud
                    width: parent.width; spacing: 20
                    Button { width: (parent.width - 20) / 2; text: "Previous chapter"; onClicked: simulator.turnReader(-1) }
                    Button { width: (parent.width - 20) / 2; text: "Next chapter"; onClicked: simulator.turnReader(1) }
                }
                Button { text: "Back to Readest Sync"; onClicked: simulator.closeReader() }
            }
        }

    }
    appControls: Component {
        Column {
            spacing: 18
            Label { width: parent.width; text: "Real Readest Sync app · simulated device services" }
            Label { width: parent.width; text: "Cloud service"; font.bold: true }
            Row {
                width: parent.width; spacing: 12
                enabled: !appController.view.busy && simulator.readerPath.length === 0
                opacity: enabled ? 1 : 0.5
                Button { width: (parent.width - 12) / 2; text: "Mock cloud"; selected: !simulator.realCloud; onClicked: simulator.switchCloud(false) }
                Button { width: (parent.width - 12) / 2; text: "Real Readest"; selected: simulator.realCloud; onClicked: simulator.switchCloud(true) }
            }
            Label { width: parent.width; text: simulator.realCloud ? "Uses your real Readest account and library over HTTPS. Sessions and downloads are separate from mock mode. Switching restarts the app; reconnect the browser if needed." : "Uses local fixtures, with no cloud account needed. Switching restarts the app and preserves each mode’s data." }
            Button {
                visible: !simulator.realCloud
                text: "Sign in to demo account"
                enabled: !appController.view.busy && !appController.view.signedIn
                opacity: enabled ? 1 : 0.5
                onClicked: { appController.signIn("demo@example.test", "demo"); overlay.panelOpen = false }
            }
            Button { visible: simulator.realCloud; text: "Enter Readest credentials"; onClicked: overlay.panelOpen = false }
            Label { width: parent.width; visible: !simulator.realCloud; text: "Refresh library after sign-in. Book 01 supports download and reading; the 51-book library also includes missing covers, position-only entries and duplicate EPUBs." }
            Label { width: parent.width; visible: !simulator.realCloud; text: "Cloud / download behavior"; font.bold: true }
            Repeater {
                model: simulator.realCloud ? [] : ["Normal", "Slow requests / downloads", "Server error (HTTP 503)", "Truncated downloads", "Corrupted downloads"]
                delegate: Button {
                    required property string modelData
                    required property int index
                    text: modelData; selected: overlay.transferMode === index
                    onClicked: { overlay.transferMode = index; simulator.transfer(index) }
                }
            }
            Label { width: parent.width; visible: !simulator.realCloud; text: "Change Readest position"; font.bold: true }
            Label { width: parent.width; visible: !simulator.realCloud; text: "Applies to the last opened book, initially book 01. Change both sides to exercise conflicts." }
            Row {
                visible: !simulator.realCloud
                width: parent.width; spacing: 12
                Repeater {
                    model: 3
                    delegate: Button {
                        required property int index
                        width: (parent.width - 24) / 3
                        text: "Chapter " + (index + 1)
                        onClicked: simulator.remoteChapter(index + 1)
                    }
                }
            }
        }
    }
}
