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
    property var view: appController.view
    property real gap: Math.max(12, width / 60)
    property real buttonHeight: Math.max(48, height / 23)
    property bool resumeOnActivation: true
    property string signInNotice: ""
    onWidthChanged: appController.setLandscape(width > height)
    onHeightChanged: appController.setLandscape(width > height)
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
                             !window.view.signedIn ? signInPage : window.view.detail ? detailPage : libraryPage
        }
    }
    component ActionButton: TextButton {
        height: window.buttonHeight
    }
    component BodyText: Text {
        color: "black"
        font.pixelSize: Math.max(18, window.width / 43)
        wrapMode: Text.Wrap
    }
    Component {
        id: busyPage
        Column {
            spacing: window.gap
            BodyText { width: parent.width; text: window.view.status || "Working…" }
            ActionButton { width: parent.width; text: "Cancel and exit"; onAction: appController.close() }
        }
    }
    Component {
        id: errorPage
        Column {
            spacing: window.gap
            BodyText { width: parent.width; text: window.view.status || "Starting…" }
            ActionButton { width: parent.width; text: "Exit"; onAction: appController.close() }
        }
    }
    Component {
        id: signInPage
        Column {
            spacing: window.gap
            BodyText { width: parent.width; text: "Sign in to your Readest library" }
            BodyText { width: parent.width; visible: text.length > 0; text: window.signInNotice }
            FramedTextInput {
                id: email
                objectName: "emailInput"
                width: parent.width; height: window.buttonHeight
                placeholderText: "Email"
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText
            }
            FramedTextInput {
                id: password
                objectName: "passwordInput"
                width: parent.width; height: window.buttonHeight
                placeholderText: "Password"
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            }
            ActionButton {
                width: parent.width; text: "Sign in"
                onAction: { var secret = password.text; password.text = ""; appController.signIn(email.text, secret) }
            }
            BodyText { width: parent.width; text: window.view.status || "" }
        }
    }
    Component {
        id: libraryPage
        Item {
            Column {
                id: toolbar
                width: parent.width
                spacing: window.gap
                Row {
                    width: parent.width; spacing: window.gap
                    ActionButton { width: (parent.width - window.gap) / 2; text: "Refresh library"; onAction: appController.activate(0) }
                    ActionButton { width: (parent.width - window.gap) / 2; text: "Scan device"; onAction: appController.scanDevice() }
                }
                FramedTextInput {
                    id: search
                    objectName: "searchInput"
                    width: parent.width; height: window.buttonHeight
                    placeholderText: "Search title or author"
                    text: window.view.filter || ""
                    onTextEdited: appController.search(text)
                }
                Row {
                    width: parent.width; spacing: window.gap / 2
                    Repeater {
                        model: ["All books", "Available to download", "On device", "Progress only"]
                        delegate: Rectangle {
                            required property int index
                            required property string modelData
                            width: (toolbar.width - 3 * window.gap / 2) / 4
                            height: window.buttonHeight
                            color: window.view.availabilityFilter === index ? "black" : "white"
                            border.color: "black"
                            Text {
                                anchors { fill: parent; margins: 3 }
                                text: modelData; color: parent.color == "#000000" ? "white" : "black"
                                font.pixelSize: Math.max(14, window.width / 65)
                                wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                            }
                            MouseArea { anchors.fill: parent; onClicked: appController.setAvailabilityFilter(index) }
                        }
                    }
                }
            }
            Grid {
                id: libraryGrid
                objectName: "libraryGrid"
                anchors { top: toolbar.bottom; topMargin: window.gap; left: parent.left; right: parent.right; bottom: footer.top; bottomMargin: window.gap }
                columns: window.width > window.height ? 4 : 3
                rows: window.width > window.height ? 1 : 2
                spacing: window.gap
                Repeater {
                    model: window.view.books || []
                    delegate: Rectangle {
                        required property var modelData
                        width: (libraryGrid.width - (libraryGrid.columns - 1) * libraryGrid.spacing) / libraryGrid.columns
                        height: (libraryGrid.height - (libraryGrid.rows - 1) * libraryGrid.spacing) / libraryGrid.rows
                        color: tap.pressed ? "#dddddd" : "white"
                        border.color: "black"
                        clip: true
                        Column {
                            anchors { fill: parent; margins: window.gap / 2 }
                            spacing: window.gap / 3
                            Item {
                                width: parent.width; height: Math.max(30, parent.height - window.buttonHeight * 2 - progressText.height - 4 * parent.spacing)
                                Image {
                                    id: cover
                                    anchors.fill: parent
                                    source: modelData.cover ? "image://cover/" + encodeURIComponent(modelData.cover) : ""
                                    sourceSize.width: Math.min(1024, width)
                                    sourceSize.height: Math.min(1024, height)
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit
                                }
                                BodyText {
                                    anchors.fill: parent
                                    visible: cover.status !== Image.Ready
                                    text: modelData.title
                                    font.pixelSize: Math.max(16, window.width / 55)
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            BodyText { width: parent.width; height: window.buttonHeight; maximumLineCount: 2; elide: Text.ElideRight; text: modelData.title; font.bold: true; font.pixelSize: Math.max(16, window.width / 55) }
                            BodyText { width: parent.width; height: window.buttonHeight / 2; elide: Text.ElideRight; maximumLineCount: 1; text: modelData.author; font.pixelSize: Math.max(14, window.width / 65) }
                            Rectangle {
                                width: parent.width; height: window.buttonHeight / 2; color: "#dddddd"
                                Text { anchors.fill: parent; text: modelData.availability; color: "black"; font.pixelSize: Math.max(14, window.width / 65); horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                            }
                            BodyText {
                                id: progressText
                                objectName: "bookProgress"
                                width: parent.width
                                text: "PocketBook: " + modelData.localProgress + "\nReadest: " + modelData.readestProgress
                                font.pixelSize: Math.max(14, window.width / 65)
                                maximumLineCount: 2
                            }
                        }
                        MouseArea { id: tap; anchors.fill: parent; onClicked: appController.activate(modelData.index) }
                    }
                }
            }
            BodyText {
                anchors.centerIn: libraryGrid
                visible: window.view.count === 0
                text: "No books. Refresh your library or change the search."
                width: libraryGrid.width
                horizontalAlignment: Text.AlignHCenter
            }
            Column {
                id: footer
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                spacing: window.gap / 2
                Row {
                    width: parent.width; spacing: window.gap
                    ActionButton { width: (parent.width-window.gap)/2; text: "Previous"; enabled: !!window.view.canPrevious; onAction: appController.turnPage(-1) }
                    ActionButton { width: (parent.width-window.gap)/2; text: "Next"; enabled: !!window.view.canNext; onAction: appController.turnPage(1) }
                }
                BodyText { width: parent.width; text: (window.view.count || 0) + " books · Page " + (window.view.page || 1) + " / " + (window.view.pages || 1); horizontalAlignment: Text.AlignHCenter }
                BodyText { width: parent.width; text: window.view.status || ""; font.pixelSize: Math.max(14, window.width / 65); maximumLineCount: 3; elide: Text.ElideRight }
                ActionButton { width: parent.width; text: "Sign out"; onAction: appController.activate(window.view.actions.length - 2) }
            }
        }
    }
    Component {
        id: detailPage
        Flickable {
            contentHeight: detailColumn.height
            clip: true
            Column {
                id: detailColumn
                width: parent.width
                spacing: window.gap
                BodyText { width: parent.width; text: window.view.hint || "" }
                BodyText { width: parent.width; text: window.view.status || "" }
                Repeater {
                    model: window.view.actions || []
                    delegate: ActionButton {
                        required property var modelData
                        width: detailColumn.width
                        text: modelData.text
                        onAction: appController.activate(modelData.index)
                    }
                }
            }
        }
    }
}
