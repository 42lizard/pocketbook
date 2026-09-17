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
    onWidthChanged: appController.setPageCapacity(width > height ? 4 : 6)
    onHeightChanged: appController.setPageCapacity(width > height ? 4 : 6)
    onActiveChanged: if (active && resumeOnActivation) appController.resume()
    onClosing: function(event) { event.accepted = false; appController.close() }

    AppHeader {
        id: header
        width: parent.width
        title: window.view.title || "Readest Sync"
        onClose: appController.close()
    }
    FocusScope {
        id: page
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom; margins: window.gap }
        focus: true
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape || event.key === Qt.Key_Home) {
                appController.back(); event.accepted = true
            } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_Right) {
                appController.turnPage(1); event.accepted = true
            } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_Left) {
                appController.turnPage(-1); event.accepted = true
            }
        }
        Loader {
            anchors.fill: parent
            sourceComponent: window.view.busy ? busyPage : !window.view.initialized ? errorPage :
                             window.view.signingIn ? signInPage : window.view.detail ? detailPage : libraryPage
        }
    }
    Component { id: busyPage; BusyPage { shell: window } }
    Component { id: errorPage; ErrorPage { shell: window } }
    Component { id: signInPage; SignInPage { shell: window } }
    Component { id: libraryPage; LibraryPage { shell: window } }
    Component { id: detailPage; BookDetailsPage { shell: window } }
}
