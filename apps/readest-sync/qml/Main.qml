import QtQuick
import QtQuick.Window
import com.pocketbook.controls

Window {
    id: window
    width: screenWidth
    height: screenHeight
    visible: true
    color: "white"
    title: "Readest Sync"
    property var view: appController
    property real gap: Math.max(12, width / 60)
    property real buttonHeight: Math.max(48, height / 23)
    property bool resumeOnActivation: true
    property string signInNotice: ""
    onWidthChanged: appController.setPageCapacity(width > height ? 6 : 8)
    onHeightChanged: appController.setPageCapacity(width > height ? 6 : 8)
    onActiveChanged: if (active && resumeOnActivation) appController.resume()
    onClosing: function(event) { event.accepted = false; appController.close() }

    // THROWAWAY PROTOTYPE: complete decided interaction system.
    LibraryPrototypePage { anchors.fill: parent; shell: window }
}
