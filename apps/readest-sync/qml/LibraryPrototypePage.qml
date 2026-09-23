// THROWAWAY PROTOTYPE. Complete PocketBook-native interaction system for Wayfinder #14.
import QtQuick
import com.pocketbook.controls

FocusScope {
    id: root
    objectName: "completePrototype"
    required property var shell
    focus: true

    property int screen: 1 // sign-in, library, details, operation, warning, conflict, uncertain
    property int returnScreen: 1
    property bool stateBarVisible: true
    property bool menuOpen: false
    property bool contextOpen: false
    property bool searchMode: false
    property bool filterActive: false
    property string statusMessage: "Library updated"
    property string operationTitle: "Synchronizing reading position"
    property string selectedTitle: "Book 01 — Native systems"
    property string selectedAuthor: "Example Author"
    property string selectedAvailability: "On device"
    property string selectedLocalProgress: "42%"
    property string selectedReadestProgress: "67%"
    property string selectedCoverPath: ""
    property var stateNames: ["Sign in", "Library", "Book details", "Foreground operation", "Recoverable warning", "Position conflict", "Uncertain commit"]
    property var libraryMenu: ["Search", "Filter: On device", "Refresh library", "Scan device", "Account"]
    property var detailMenu: ["Sync now", "Upload cover", "Choose local copy", "Account"]

    readonly property real scale: Math.min(width, height) / 1404
    readonly property real stroke: Math.max(6, 10 * scale)
    readonly property real s: 16 * scale
    readonly property real m: 24 * scale
    readonly property real l: 32 * scale
    readonly property real chrome: 138 * scale
    readonly property real bookRow: 199 * scale
    readonly property real menuRow: 123 * scale
    readonly property real action: 134 * scale
    readonly property real input: 139 * scale
    readonly property real status: 96 * scale
    readonly property real titleFont: 44 * scale
    readonly property real bodyFont: 38 * scale
    readonly property real metaFont: 34 * scale
    readonly property real smallFont: 30 * scale

    function showScreen(value) {
        screen = Math.max(0, Math.min(stateNames.length - 1, value))
        menuOpen = false; contextOpen = false; searchMode = false
    }
    function chooseBook(title, author, availability, localProgress, readestProgress, coverPath) {
        selectedTitle = title; selectedAuthor = author; selectedAvailability = availability
        selectedLocalProgress = localProgress; selectedReadestProgress = readestProgress
        selectedCoverPath = coverPath; showScreen(2)
    }
    function hardwareButton(key) {
        if (key === Qt.Key_Back || key === Qt.Key_Escape || key === Qt.Key_Home) {
            if (menuOpen || contextOpen) { menuOpen = false; contextOpen = false }
            else if (searchMode) { searchMode = false; shell.view.search("") }
            else if (screen === 6) {} // uncertain state requires explicit acknowledgement
            else if (screen === 3 || screen === 5) showScreen(2)
            else if (screen !== 1) showScreen(1)
        } else if ((screen === 1 || screen === 4) && (key === Qt.Key_PageDown || key === Qt.Key_Right)) shell.view.turnPage(1)
        else if ((screen === 1 || screen === 4) && (key === Qt.Key_PageUp || key === Qt.Key_Left)) shell.view.turnPage(-1)
    }
    Keys.onPressed: function(event) { root.hardwareButton(event.key); event.accepted = true }

    Rectangle { anchors.fill: parent; color: "white" }
    AppHeader {
        id: header
        width: parent.width; height: root.chrome
        title: root.screen === 0 ? "SIGN IN / READEST SYNC" :
               root.screen === 1 || root.screen === 4 ? "Readest Sync" : "BOOK INFO / " + root.selectedTitle
        onClose: root.menuOpen = !root.menuOpen
    }
    Rectangle { anchors { left: parent.left; right: parent.right; bottom: header.bottom } height: 1; color: "black" }

    Rectangle {
        z: 4; visible: !root.searchMode && root.screen !== 1 && root.screen !== 4
        anchors { left: parent.left; top: parent.top } width: root.menuRow; height: root.chrome
        color: backTap.pressed ? "black" : "white"
        Text { anchors.centerIn: parent; text: "‹"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.titleFont * 1.5 }
        MouseArea { id: backTap; anchors.fill: parent; onClicked: root.hardwareButton(Qt.Key_Back) }
    }
    Rectangle {
        z: 4; visible: !root.searchMode && root.screen !== 0 && root.screen !== 3 && root.screen !== 5 && root.screen !== 6
        anchors { right: parent.right; top: parent.top } width: root.menuRow * 1.35; height: root.chrome
        color: menuTap.pressed || root.menuOpen ? "black" : "white"
        Text { anchors.centerIn: parent; text: "Menu"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.metaFont }
        MouseArea { id: menuTap; anchors.fill: parent; onClicked: root.menuOpen = !root.menuOpen }
    }
    Rectangle {
        z: 5; visible: root.searchMode; anchors { left: parent.left; right: parent.right; top: parent.top } height: root.chrome; color: "white"
        Rectangle {
            id: searchBack; anchors { left: parent.left; top: parent.top; bottom: parent.bottom } width: root.menuRow; color: searchBackTap.pressed ? "black" : "white"
            Text { anchors.centerIn: parent; text: "‹"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.titleFont * 1.5 }
            MouseArea { id: searchBackTap; anchors.fill: parent; onClicked: { root.searchMode = false; root.shell.view.search("") } }
        }
        FramedTextInput {
            id: searchInput; objectName: "prototypeSearchInput"
            anchors { left: searchBack.right; right: clearSearch.left; verticalCenter: parent.verticalCenter }
            height: root.input; placeholderText: "Search title or author"; font.pixelSize: root.bodyFont
            onTextEdited: root.shell.view.search(text)
        }
        Rectangle {
            id: clearSearch; anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: root.menuRow
            color: clearTap.pressed ? "black" : "white"
            Text { anchors.centerIn: parent; text: "×"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.titleFont }
            MouseArea { id: clearTap; anchors.fill: parent; onClicked: { searchInput.text = ""; root.shell.view.search("") } }
        }
    }

    Loader {
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
        sourceComponent: root.screen === 0 ? signInSurface : root.screen === 1 || root.screen === 4 ? librarySurface : detailSurface
    }

    Component {
        id: signInSurface
        Item {
            Column {
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: root.l } spacing: root.m
                Text { width: parent.width; text: "Sign in to your Readest library"; font.pixelSize: root.titleFont; font.bold: true; wrapMode: Text.Wrap }
                Text { width: parent.width; text: "Downloaded books remain available offline."; font.pixelSize: root.metaFont; wrapMode: Text.Wrap }
                Text { text: "Email"; font.pixelSize: root.metaFont; font.bold: true }
                FramedTextInput { id: email; objectName: "prototypeEmail"; width: parent.width; height: root.input; placeholderText: "name@example.com"; font.pixelSize: root.bodyFont }
                Text { text: "Password"; font.pixelSize: root.metaFont; font.bold: true }
                FramedTextInput { id: password; objectName: "prototypePassword"; width: parent.width; height: root.input; placeholderText: "Password"; font.pixelSize: root.bodyFont; echoMode: TextInput.Password }
                Text { visible: email.text.length > 0 && email.text.indexOf("@") < 0; text: "Enter a complete email address."; font.pixelSize: root.smallFont }
                Rectangle {
                    width: parent.width; height: root.action; color: signInTap.pressed ? "#444444" : "black"
                    Text { anchors.centerIn: parent; text: "Sign in"; color: "white"; font.pixelSize: root.bodyFont }
                    MouseArea { id: signInTap; anchors.fill: parent; onClicked: { root.returnScreen = 1; root.operationTitle = "Signing in"; root.showScreen(3) } }
                }
                Rectangle {
                    width: parent.width; height: root.action; color: backLibraryTap.pressed ? "black" : "white"; border.color: "black"
                    Text { anchors.centerIn: parent; text: "Back to library"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.bodyFont }
                    MouseArea { id: backLibraryTap; anchors.fill: parent; onClicked: root.showScreen(1) }
                }
            }
        }
    }

    Component {
        id: librarySurface
        Item {
            Column {
                id: bands; anchors { left: parent.left; right: parent.right; top: parent.top }
                Rectangle {
                    width: parent.width; height: root.searchMode || root.filterActive ? root.menuRow : 0; visible: height > 0; color: "white"; border.color: "black"
                    Text {
                        anchors { left: parent.left; right: clearFilter.left; verticalCenter: parent.verticalCenter; leftMargin: root.m }
                        text: root.searchMode && searchInput.text.length ? "Search: " + searchInput.text : "On device · " + (root.shell.view.library.count || 0) + " books"
                        font.pixelSize: root.metaFont; elide: Text.ElideRight
                    }
                    Rectangle {
                        id: clearFilter; anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: root.menuRow
                        color: filterTap.pressed ? "black" : "white"
                        Text { anchors.centerIn: parent; text: "Clear"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.smallFont }
                        MouseArea { id: filterTap; anchors.fill: parent; onClicked: { root.filterActive = false; root.searchMode = false; root.shell.view.search(""); root.shell.view.setAvailabilityFilter(0) } }
                    }
                }
                Rectangle {
                    width: parent.width; height: root.screen === 4 ? root.menuRow * 1.35 : 0; visible: height > 0; color: "white"; border { color: "black"; width: root.stroke }
                    Text { anchors { left: parent.left; right: retry.left; verticalCenter: parent.verticalCenter; margins: root.m } text: "Readest could not refresh. Downloaded books still work offline."; font.pixelSize: root.metaFont; wrapMode: Text.Wrap }
                    Rectangle {
                        id: retry; anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: root.menuRow * 1.7; color: retryTap.pressed ? "black" : "white"; border.color: "black"
                        Text { anchors.centerIn: parent; text: "Retry"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.metaFont }
                        MouseArea { id: retryTap; anchors.fill: parent; onClicked: { root.returnScreen = 1; root.showScreen(3) } }
                    }
                }
                Rectangle {
                    width: parent.width; height: root.statusMessage.length && root.screen === 1 ? root.status : 0; visible: height > 0; color: "white"
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "black" }
                    Text { anchors { fill: parent; leftMargin: root.m } text: root.statusMessage; font.pixelSize: root.metaFont; verticalAlignment: Text.AlignVCenter }
                    MouseArea { anchors.fill: parent; onClicked: root.statusMessage = "" }
                }
            }
            ListView {
                id: books; objectName: "prototypeLibraryList"
                anchors { left: parent.left; right: parent.right; top: bands.bottom; bottom: pageIndicator.top }
                clip: true; model: root.shell.view.library; boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: row; objectName: "detailedBookRow"
                    required property string bookTitle; required property string author; required property string availability
                    required property string coverPath; required property string localProgress; required property string readestProgress
                    width: ListView.view.width; height: root.bookRow; color: rowTap.pressed ? "black" : "white"
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "black" }
                    Image {
                        id: cover; anchors { left: parent.left; top: parent.top; leftMargin: 79 * root.scale; topMargin: root.s }
                        width: 105 * root.scale; height: 155 * root.scale
                        source: row.coverPath ? "image://cover/" + encodeURIComponent(row.coverPath) : ""
                        sourceSize.width: Math.min(1024, width)
                        sourceSize.height: Math.min(1024, height)
                        fillMode: Image.PreserveAspectFit; asynchronous: true
                    }
                    Rectangle { anchors.fill: cover; visible: cover.status !== Image.Ready; color: rowTap.pressed ? "black" : "#eeeeee"; Text { anchors.centerIn: parent; text: "Book"; color: rowTap.pressed ? "white" : "black"; font.pixelSize: root.smallFont } }
                    Column {
                        anchors { left: cover.right; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: root.m; rightMargin: root.l } spacing: 5 * root.scale
                        Text { width: parent.width; text: row.bookTitle; color: rowTap.pressed ? "white" : "black"; font.pixelSize: root.titleFont; font.bold: true; elide: Text.ElideRight }
                        Text { width: parent.width; text: row.author; color: rowTap.pressed ? "white" : "black"; font.pixelSize: root.bodyFont; elide: Text.ElideRight }
                        Text { width: parent.width; text: row.localProgress && row.localProgress !== "—" ? row.localProgress + " read" : row.availability; color: rowTap.pressed ? "white" : "black"; font.pixelSize: root.metaFont; font.italic: true; elide: Text.ElideRight }
                    }
                    MouseArea {
                        id: rowTap; anchors.fill: parent
                        onClicked: { root.statusMessage = ""; root.chooseBook(row.bookTitle, row.author, row.availability, row.localProgress, row.readestProgress, row.coverPath) }
                        onPressAndHold: { root.selectedTitle = row.bookTitle; root.selectedAuthor = row.author; root.selectedAvailability = row.availability; root.selectedLocalProgress = row.localProgress; root.selectedReadestProgress = row.readestProgress; root.selectedCoverPath = row.coverPath; root.contextOpen = true }
                    }
                }
            }
            Text { anchors.centerIn: books; visible: root.shell.view.library.count === 0; text: "No books"; font.pixelSize: root.titleFont * 1.25 }
            Rectangle {
                id: pageIndicator; anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: root.status; color: "white"
                Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: "black" }
                Text { anchors.centerIn: parent; text: (root.shell.view.library.count || 0) + " books · Page " + (root.shell.view.library.page || 1) + " / " + (root.shell.view.library.pages || 1); font.pixelSize: root.smallFont }
            }
        }
    }

    Component {
        id: detailSurface
        Item {
            Flickable {
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: primary.top } contentHeight: details.height; clip: true
                Column {
                    id: details; width: parent.width; spacing: root.l; padding: root.l
                    Row {
                        width: parent.width - details.leftPadding - details.rightPadding; spacing: root.l
                        Item {
                            width: Math.min(390 * root.scale, parent.width * 0.36); height: width * 1.48
                            Image {
                                id: detailCover; anchors.fill: parent
                                source: root.selectedCoverPath ? "image://cover/" + encodeURIComponent(root.selectedCoverPath) : ""
                                sourceSize.width: Math.min(1024, width)
                                sourceSize.height: Math.min(1024, height)
                                fillMode: Image.PreserveAspectFit
                            }
                            Rectangle { anchors.fill: parent; visible: detailCover.status !== Image.Ready; color: "#eeeeee"; Text { anchors.centerIn: parent; text: "Book cover"; font.pixelSize: root.metaFont } }
                        }
                        Column {
                            width: parent.width - Math.min(390 * root.scale, parent.width * 0.36) - parent.spacing; spacing: root.s
                            Text { text: "♡"; font.pixelSize: root.titleFont * 1.5 }
                            Text { text: "Title"; font.pixelSize: root.metaFont; font.bold: true }
                            Text { width: parent.width; text: root.selectedTitle; font.pixelSize: root.bodyFont; wrapMode: Text.Wrap }
                            Rectangle { width: parent.width; height: 1; color: "black" }
                            Text { text: "Author"; font.pixelSize: root.metaFont; font.bold: true }
                            Text { width: parent.width; text: root.selectedAuthor; font.pixelSize: root.bodyFont; wrapMode: Text.Wrap }
                            Rectangle { width: parent.width; height: 1; color: "black" }
                            Text { text: "Availability"; font.pixelSize: root.metaFont; font.bold: true }
                            Text { text: root.selectedAvailability; font.pixelSize: root.bodyFont }
                        }
                    }
                    Rectangle { width: parent.width - details.leftPadding - details.rightPadding; height: 1; color: "black" }
                    Text { text: "READING PROGRESS"; font.pixelSize: root.metaFont; font.bold: true }
                    Row {
                        width: parent.width - details.leftPadding - details.rightPadding; spacing: root.l
                        Column { width: (parent.width - parent.spacing) / 2; Text { text: "PocketBook"; font.pixelSize: root.metaFont; font.bold: true } Text { text: root.selectedLocalProgress || "—"; font.pixelSize: root.titleFont } }
                        Column { width: (parent.width - parent.spacing) / 2; Text { text: "Readest"; font.pixelSize: root.metaFont; font.bold: true } Text { text: root.selectedReadestProgress || "—"; font.pixelSize: root.titleFont } }
                    }
                    Rectangle { width: parent.width - details.leftPadding - details.rightPadding; height: 1; color: "black" }
                    Text { text: "ACTIONS"; font.pixelSize: root.metaFont; font.bold: true }
                    Repeater {
                        model: ["Sync now", "Read offline", "Choose local copy"]
                        delegate: Rectangle {
                            required property string modelData; width: details.width - details.leftPadding - details.rightPadding; height: root.menuRow
                            color: secondaryTap.pressed ? "black" : "white"; border.color: "black"
                            Text { anchors { fill: parent; leftMargin: root.m } text: modelData; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.bodyFont; verticalAlignment: Text.AlignVCenter }
                            MouseArea { id: secondaryTap; anchors.fill: parent; onClicked: if (index === 0) root.showScreen(5) }
                        }
                    }
                }
            }
            Rectangle {
                id: primary; objectName: "prototypePrimaryAction"; anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: root.action
                color: primaryTap.pressed ? "#444444" : "black"
                Text { anchors.centerIn: parent; text: "Open at PocketBook position"; color: "white"; font.pixelSize: root.bodyFont }
                MouseArea { id: primaryTap; anchors.fill: parent; onClicked: { root.returnScreen = 2; root.operationTitle = "Opening book"; root.showScreen(3) } }
            }
        }
    }

    Rectangle {
        id: appMenu; objectName: "prototypeAppMenu"; z: 20; visible: root.menuOpen
        anchors { right: parent.right; top: header.bottom } width: Math.min(parent.width * 0.7, 662 * root.scale); height: menuItems.height + 2 * root.stroke
        color: "white"; border { color: "black"; width: root.stroke }
        Column {
            id: menuItems; anchors { left: parent.left; right: parent.right; top: parent.top; margins: root.stroke }
            Repeater {
                model: root.screen === 2 ? root.detailMenu : root.libraryMenu
                delegate: Rectangle {
                    required property int index; required property string modelData; width: menuItems.width; height: root.menuRow
                    color: appMenuTap.pressed ? "black" : "white"
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "black" }
                    Text { anchors { fill: parent; leftMargin: root.m } text: modelData; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.bodyFont; verticalAlignment: Text.AlignVCenter }
                    MouseArea {
                        id: appMenuTap; anchors.fill: parent
                        onClicked: {
                            root.menuOpen = false
                            if (root.screen === 2) { if (index === 0) root.showScreen(3); else if (index === 3) root.showScreen(0) }
                            else if (index === 0) root.searchMode = true
                            else if (index === 1) { root.filterActive = true; root.shell.view.setAvailabilityFilter(2) }
                            else if (index === 2) { root.statusMessage = "Refreshing library"; root.shell.view.refreshLibrary() }
                            else if (index === 3) { root.statusMessage = "Scanning device"; root.shell.view.scanDevice() }
                            else root.showScreen(0)
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: contextMenu; objectName: "prototypeContextMenu"; z: 21; visible: root.contextOpen
        anchors { right: parent.right; top: header.bottom; topMargin: root.bookRow } width: Math.min(parent.width * 0.7, 662 * root.scale); height: contextItems.height + 2 * root.stroke
        color: "white"; border { color: "black"; width: root.stroke }
        Column {
            id: contextItems; anchors { left: parent.left; right: parent.right; top: parent.top; margins: root.stroke }
            Repeater {
                model: ["Open", "Book info", "Sync now", "Read offline"]
                delegate: Rectangle {
                    required property int index; required property string modelData; width: contextItems.width; height: root.menuRow
                    color: contextTap.pressed ? "black" : "white"; border.color: "black"
                    Text { anchors { fill: parent; leftMargin: root.m } text: modelData; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.bodyFont; verticalAlignment: Text.AlignVCenter }
                    MouseArea { id: contextTap; anchors.fill: parent; onClicked: { root.contextOpen = false; if (index === 1) root.showScreen(2); else if (index === 2) root.showScreen(5); else if (index === 0) root.showScreen(3) } }
                }
            }
        }
    }

    Rectangle { z: 30; visible: root.screen === 3 || root.screen === 5 || root.screen === 6; anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom } color: "#dddddd"; opacity: 0.82 }

    Rectangle {
        id: operationDialog; objectName: "prototypeOperationSurface"; z: 31; visible: root.screen === 3
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: root.stateBarVisible ? root.action : root.l }
        width: Math.min(parent.width - 2 * root.l, 1200 * root.scale); height: 540 * root.scale; color: "white"; border { color: "black"; width: root.stroke }
        Column {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: root.l } spacing: root.l
            Text { width: parent.width; text: root.operationTitle; font.pixelSize: root.titleFont; font.bold: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
            Text { width: parent.width; text: "Connecting to Wi-Fi"; font.pixelSize: root.bodyFont; horizontalAlignment: Text.AlignHCenter }
            Rectangle { width: parent.width; height: root.l; color: "white"; border.color: "black"; Rectangle { width: parent.width * 0.58; height: parent.height; color: "black" } }
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: root.action; color: cancelTap.pressed ? "black" : "white"; border.color: "black"
            Text { anchors.centerIn: parent; text: "Cancel and exit"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.bodyFont }
            MouseArea { id: cancelTap; anchors.fill: parent; onClicked: root.showScreen(root.returnScreen) }
        }
    }

    Rectangle {
        id: conflictDialog; objectName: "prototypeConflictDialog"; z: 31; visible: root.screen === 5
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: root.stateBarVisible ? root.action : root.l }
        width: Math.min(parent.width - 2 * root.l, 1200 * root.scale); height: 574 * root.scale; color: "white"; border { color: "black"; width: root.stroke }
        Column {
            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: conflictActions.top; margins: root.l } spacing: root.m
            Text { width: parent.width; text: "Reading positions differ"; font.pixelSize: root.titleFont; font.bold: true; horizontalAlignment: Text.AlignHCenter }
            Text { width: parent.width; text: "PocketBook  " + root.selectedLocalProgress + "    ·    Readest  " + root.selectedReadestProgress; font.pixelSize: root.bodyFont; horizontalAlignment: Text.AlignHCenter }
            Text { width: parent.width; text: "Choose which position should become authoritative."; font.pixelSize: root.metaFont; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        }
        Row {
            id: conflictActions; anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: root.action
            Repeater {
                model: ["Use PocketBook", "Use Readest", "Cancel"]
                delegate: Rectangle {
                    required property string modelData; width: conflictActions.width / 3; height: conflictActions.height; color: conflictTap.pressed ? "black" : "white"; border.color: "black"
                    Text { anchors { fill: parent; margins: root.s } text: modelData; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.metaFont; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; wrapMode: Text.Wrap }
                    MouseArea { id: conflictTap; anchors.fill: parent; onClicked: root.showScreen(2) }
                }
            }
        }
    }

    Rectangle {
        id: uncertainDialog; objectName: "prototypeUncertainDialog"; z: 31; visible: root.screen === 6
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: root.stateBarVisible ? root.action : root.l }
        width: Math.min(parent.width - 2 * root.l, 1200 * root.scale); height: 650 * root.scale; color: "white"; border { color: "black"; width: root.stroke }
        Column {
            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: acknowledge.top; margins: root.l } spacing: root.m
            Text { width: parent.width; text: "PocketBook state is uncertain"; font.pixelSize: root.titleFont; font.bold: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
            Text { width: parent.width; text: "The Readest position may have been applied, but PocketBook did not confirm the saved position."; font.pixelSize: root.bodyFont; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
            Text { width: parent.width; text: "Open remains unavailable until a fresh Sync now succeeds."; font.pixelSize: root.metaFont; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        }
        Rectangle {
            id: acknowledge; anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: root.action; color: acknowledgeTap.pressed ? "black" : "white"; border.color: "black"
            Text { anchors.centerIn: parent; text: "Acknowledge"; color: parent.color === "#000000" ? "white" : "black"; font.pixelSize: root.bodyFont }
            MouseArea { id: acknowledgeTap; anchors.fill: parent; onClicked: root.showScreen(2) }
        }
    }

    Rectangle {
        id: stateBar; objectName: "prototypeStateBar"; z: 50; visible: root.stateBarVisible
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: root.status; color: "black"
        Row {
            anchors.fill: parent
            Rectangle { width: parent.width * 0.18; height: parent.height; color: previousState.pressed ? "#555555" : "black"; Text { anchors.centerIn: parent; text: "‹"; color: "white"; font.pixelSize: root.titleFont } MouseArea { id: previousState; anchors.fill: parent; onClicked: root.showScreen((root.screen + root.stateNames.length - 1) % root.stateNames.length) } }
            Rectangle { width: parent.width * 0.64; height: parent.height; color: "black"; Text { anchors.centerIn: parent; text: "PROTOTYPE · " + root.stateNames[root.screen] + " · tap to hide"; color: "white"; font.pixelSize: root.smallFont; font.bold: true } MouseArea { anchors.fill: parent; onClicked: root.stateBarVisible = false } }
            Rectangle { width: parent.width * 0.18; height: parent.height; color: nextState.pressed ? "#555555" : "black"; Text { anchors.centerIn: parent; text: "›"; color: "white"; font.pixelSize: root.titleFont } MouseArea { id: nextState; anchors.fill: parent; onClicked: root.showScreen((root.screen + 1) % root.stateNames.length) } }
        }
    }
    Rectangle {
        z: 50; visible: !root.stateBarVisible; anchors { right: parent.right; bottom: parent.bottom } width: root.menuRow * 1.4; height: root.status
        color: restoreStates.pressed ? "#555555" : "black"; Text { anchors.centerIn: parent; text: "States"; color: "white"; font.pixelSize: root.smallFont }
        MouseArea { id: restoreStates; anchors.fill: parent; onClicked: root.stateBarVisible = true }
    }
}
