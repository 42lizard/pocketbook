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
    property alias metrics: nativeMetrics
    property real gap: Math.max(12, width / 60)
    property real buttonHeight: Math.max(48, height / 23)
    property bool resumeOnActivation: true
    property string signInNotice: ""
    readonly property bool libraryActive: view.initialized && !view.signingIn && !view.detail
    onWidthChanged: appController.setPageCapacity(width > height ? 6 : 8)
    onHeightChanged: appController.setPageCapacity(width > height ? 6 : 8)
    onActiveChanged: if (active && resumeOnActivation) appController.resume()
    onClosing: function(event) { event.accepted = false; appController.close() }

    NativeMetrics { id: nativeMetrics; viewportWidth: window.width; viewportHeight: window.height }

    function toggleMenu() {
        if (!view.busy && !view.decision && !view.blocking &&
                pageLoader.item && pageLoader.item.toggleMenu &&
                (window.libraryActive || pageLoader.item.hasMenu))
            pageLoader.item.toggleMenu()
    }

    function handleHardwareButton(key) {
        const normalizedKey = Number(key)
        if (normalizedKey === Qt.Key_Menu) {
            window.toggleMenu()
            return
        }
        if (pageLoader.item && pageLoader.item.handleKey && pageLoader.item.handleKey(normalizedKey)) return
        if (normalizedKey === Qt.Key_Back || normalizedKey === Qt.Key_Escape || normalizedKey === Qt.Key_Home) appController.back()
        else if (normalizedKey === Qt.Key_PageDown || normalizedKey === Qt.Key_Right) appController.turnPage(1)
        else if (normalizedKey === Qt.Key_PageUp || normalizedKey === Qt.Key_Left) appController.turnPage(-1)
    }

    NativeHeader {
        id: header
        objectName: "nativeHeader"
        width: parent.width
        height: window.metrics.chrome
        title: window.view.detail ? "BOOK INFO / " + (window.view.title || "Untitled") : window.view.title || "Readest Sync"
        showBack: window.view.detail || window.view.signingIn
        actionText: window.libraryActive || (pageLoader.item && pageLoader.item.hasMenu) ? "Menu" : ""
        onBack: window.handleHardwareButton(Qt.Key_Back)
        onAction: window.toggleMenu()
        onClose: window.view.close()
    }
    FocusScope {
        id: page
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.margins: window.libraryActive || window.view.detail ? 0 : window.gap
        focus: true
        Keys.onPressed: function(event) {
            if (event.key !== Qt.Key_Menu) window.handleHardwareButton(event.key)
            event.accepted = true
        }
        Keys.onReleased: function(event) {
            if (event.key === Qt.Key_Menu && !event.isAutoRepeat) window.handleHardwareButton(event.key)
            event.accepted = true
        }
        Loader {
            id: pageLoader
            anchors.fill: parent
            sourceComponent: !window.view.initialized ? (window.view.busy ? busyPage : errorPage) :
                             window.view.signingIn ? signInPage : window.view.detail ? detailPage : libraryPage
        }
        OperationSurface {
            anchors.fill: parent
            visible: window.view.busy && window.view.initialized
            shell: window
        }
        DecisionDialog {
            anchors.fill: parent
            visible: window.view.decision || window.view.blocking
            shell: window
        }
    }
    Component { id: busyPage; BusyPage { shell: window } }
    Component { id: errorPage; ErrorPage { shell: window } }
    Component { id: signInPage; SignInPage { shell: window } }
    Component { id: libraryPage; LibraryPage { shell: window } }
    Component { id: detailPage; BookDetailsPage { shell: window } }
}
