import QtQuick
import com.pocketbook.controls

Item {
    id: root
    objectName: "nativeLibraryPage"
    required property var shell

    property bool menuOpen: false
    property bool filterMenuOpen: false
    property bool searchOpen: false
    property bool contextOpen: false
    property bool activating: false
    property string contextAccount: ""
    property string contextHash: ""
    property string contextTitle: ""

    readonly property var filterNames: ["All books", "Available to download", "On device", "Progress only", "PocketBook only"]
    readonly property var appMenuItems: ["Search", "Filter", "Refresh library", "Scan device", shell.view.signedIn ? "Sign out" : "Sign in to Readest", "Close"]

    function closeTransientSurfaces() {
        var handled = menuOpen || filterMenuOpen || contextOpen || searchOpen
        menuOpen = false
        filterMenuOpen = false
        contextOpen = false
        if (searchOpen) {
            searchOpen = false
            shell.view.search("")
        }
        return handled
    }

    function handleKey(key) {
        if (key === Qt.Key_Back || key === Qt.Key_Escape || key === Qt.Key_Home)
            return closeTransientSurfaces()
        return false
    }

    function toggleMenu() {
        filterMenuOpen = false
        contextOpen = false
        menuOpen = !menuOpen
    }

    function openBook(account, hash) {
        if (activating) return
        activating = true
        menuOpen = false
        filterMenuOpen = false
        contextOpen = false
        shell.view.selectBook(account, hash)
    }

    function runMenuAction(index) {
        menuOpen = false
        if (index === 0) searchOpen = true
        else if (index === 1) filterMenuOpen = true
        else if (index === 2) shell.view.refreshLibrary()
        else if (index === 3) shell.view.scanDevice()
        else if (index === 4) shell.view.signedIn ? shell.view.signOut() : shell.view.showSignIn()
        else shell.view.close()
    }

    Column {
        id: topBands
        anchors { left: parent.left; right: parent.right; top: parent.top }

        Item {
            width: parent.width
            height: root.searchOpen ? shell.metrics.framedInput : 0
            visible: height > 0

            Rectangle {
                id: searchBack
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: shell.metrics.menuRow
                color: searchBackTap.pressed ? "black" : "white"
                Text {
                    anchors.centerIn: parent
                    text: "‹"
                        color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                    font.pixelSize: shell.metrics.titleFont * 1.4
                }
                MouseArea {
                    id: searchBackTap
                    anchors.fill: parent
                    onClicked: { root.searchOpen = false; shell.view.search("") }
                }
            }
            FramedTextInput {
                id: searchInput
                objectName: "searchInput"
                anchors { left: searchBack.right; right: clearSearch.left; top: parent.top; bottom: parent.bottom }
                placeholderText: "Search title or author"
                text: shell.view.library.searchText || ""
                font.pixelSize: shell.metrics.bodyFont
                onTextEdited: shell.view.search(text)
            }
            Rectangle {
                id: clearSearch
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: shell.metrics.menuRow
                color: clearSearchTap.pressed ? "black" : "white"
                Text {
                    anchors.centerIn: parent
                    text: "×"
                        color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                    font.pixelSize: shell.metrics.titleFont
                }
                MouseArea {
                    id: clearSearchTap
                    anchors.fill: parent
                    onClicked: { searchInput.text = ""; shell.view.search("") }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: shell.view.library.availabilityFilter !== 0 ? shell.metrics.footer : 0
            visible: height > 0
            color: "white"
            border.color: "black"
            Text {
                anchors { left: parent.left; right: clearFilter.left; verticalCenter: parent.verticalCenter; leftMargin: shell.metrics.gap }
                text: root.filterNames[shell.view.library.availabilityFilter] + " · " + (shell.view.library.count || 0) + " books"
                font.pixelSize: shell.metrics.metaFont
                elide: Text.ElideRight
            }
            Rectangle {
                id: clearFilter
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: shell.metrics.menuRow
                color: clearFilterTap.pressed ? "black" : "white"
                Text {
                    anchors.centerIn: parent
                    text: "Clear"
                        color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                    font.pixelSize: shell.metrics.smallFont
                }
                MouseArea { id: clearFilterTap; anchors.fill: parent; onClicked: shell.view.setAvailabilityFilter(0) }
            }
        }
    }

    ListView {
        id: books
        objectName: "nativeLibraryList"
        anchors { left: parent.left; right: parent.right; top: topBands.bottom; bottom: footer.top }
        clip: true
        model: shell.view.library
        boundsBehavior: Flickable.StopAtBounds
        interactive: false

        delegate: Rectangle {
            id: row
            objectName: "detailedBookRow"
            required property string account
            required property string bookHash
            required property string bookTitle
            required property string author
            required property string availability
            required property string coverPath
            required property string localProgress
            width: ListView.view.width
            height: shell.metrics.bookRow
            color: rowTap.pressed ? "black" : "white"

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: "black"
            }
            Image {
                id: cover
                anchors { left: parent.left; top: parent.top; leftMargin: 79 * shell.metrics.scale; topMargin: shell.metrics.smallGap }
                width: 105 * shell.metrics.scale
                height: 155 * shell.metrics.scale
                source: row.coverPath ? "image://cover/" + encodeURIComponent(row.coverPath) : ""
                sourceSize.width: Math.min(1024, width)
                sourceSize.height: Math.min(1024, height)
                asynchronous: true
                fillMode: Image.PreserveAspectFit
            }
            Rectangle {
                anchors.fill: cover
                visible: cover.status !== Image.Ready
                color: rowTap.pressed ? "black" : "#eeeeee"
                Text {
                    anchors.centerIn: parent
                    text: "Book"
                    color: rowTap.pressed ? "white" : "black"
                    font.pixelSize: shell.metrics.smallFont
                }
            }
            Column {
                anchors {
                    left: cover.right
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: shell.metrics.gap
                    rightMargin: shell.metrics.largeGap
                }
                spacing: 5 * shell.metrics.scale
                Text {
                    width: parent.width
                    text: row.bookTitle
                    color: rowTap.pressed ? "white" : "black"
                    font.pixelSize: shell.metrics.titleFont
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: row.author
                    color: rowTap.pressed ? "white" : "black"
                    font.pixelSize: shell.metrics.bodyFont
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: row.localProgress && row.localProgress !== "—" ? row.localProgress + " read" : row.availability
                    color: rowTap.pressed ? "white" : "black"
                    font.pixelSize: shell.metrics.metaFont
                    font.italic: true
                    elide: Text.ElideRight
                }
            }
            MouseArea {
                id: rowTap
                anchors.fill: parent
                property bool held: false
                onPressed: held = false
                onPressAndHold: {
                    held = true
                    root.contextAccount = row.account
                    root.contextHash = row.bookHash
                    root.contextTitle = row.bookTitle
                    root.menuOpen = false
                    root.filterMenuOpen = false
                    root.contextOpen = true
                }
                onClicked: if (!held) root.openBook(row.account, row.bookHash)
            }
        }
    }

    Text {
        anchors.centerIn: books
        visible: shell.view.library.count === 0
        width: books.width - 2 * shell.metrics.largeGap
        text: "No books. Refresh your library or change the search."
        font.pixelSize: shell.metrics.bodyFont
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    Rectangle {
        id: footer
        objectName: "nativeLibraryFooter"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: shell.metrics.footer
        color: "white"
        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: "black" }
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: shell.metrics.menuRow
            color: previousTap.pressed ? "black" : "white"
            Text { anchors.centerIn: parent; text: "‹"; color: Qt.colorEqual(parent.color, "black") ? "white" : "black"; font.pixelSize: shell.metrics.titleFont }
            MouseArea { id: previousTap; anchors.fill: parent; enabled: shell.view.library.page > 1; onClicked: shell.view.turnPage(-1) }
        }
        Column {
            anchors.centerIn: parent
            width: parent.width - 2 * shell.metrics.menuRow
            Text {
                width: parent.width
                text: (shell.view.library.count || 0) + " books · Page " + (shell.view.library.page || 1) + " / " + (shell.view.library.pages || 1)
                font.pixelSize: shell.metrics.smallFont
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: shell.view.status || ""
                font.pixelSize: shell.metrics.smallFont * 0.72
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }
        }
        Rectangle {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: shell.metrics.menuRow
            color: nextTap.pressed ? "black" : "white"
            Text { anchors.centerIn: parent; text: "›"; color: Qt.colorEqual(parent.color, "black") ? "white" : "black"; font.pixelSize: shell.metrics.titleFont }
            MouseArea { id: nextTap; anchors.fill: parent; enabled: shell.view.library.page < shell.view.library.pages; onClicked: shell.view.turnPage(1) }
        }
    }

    Rectangle {
        id: appMenu
        objectName: "nativeLibraryMenu"
        z: 20
        visible: root.menuOpen
        anchors { right: parent.right; top: parent.top }
        width: Math.min(parent.width * 0.7, 662 * shell.metrics.scale)
        height: appMenuItemsColumn.height + 2
        color: "white"
        border.color: "black"
        Column {
            id: appMenuItemsColumn
            width: parent.width - 2
            anchors.centerIn: parent
            Repeater {
                model: root.appMenuItems
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: appMenuItemsColumn.width
                    height: shell.metrics.menuRow
                    color: appMenuTap.pressed ? "black" : "white"
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "black" }
                    Text {
                        anchors { fill: parent; leftMargin: shell.metrics.gap }
                        text: modelData
                        color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                        font.pixelSize: shell.metrics.bodyFont
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea { id: appMenuTap; anchors.fill: parent; onClicked: root.runMenuAction(index) }
                }
            }
        }
    }

    Rectangle {
        id: filterMenu
        objectName: "nativeFilterMenu"
        z: 21
        visible: root.filterMenuOpen
        anchors { right: parent.right; top: parent.top }
        width: Math.min(parent.width * 0.78, 760 * shell.metrics.scale)
        height: filterItems.height + 2
        color: "white"
        border.color: "black"
        Column {
            id: filterItems
            width: parent.width - 2
            anchors.centerIn: parent
            Repeater {
                model: root.filterNames
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: filterItems.width
                    height: shell.metrics.menuRow
                    color: filterTap.pressed ? "black" : "white"
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "black" }
                    Text {
                        anchors { fill: parent; leftMargin: shell.metrics.gap; rightMargin: shell.metrics.gap }
                        text: (shell.view.library.availabilityFilter === index ? "✓  " : "") + modelData
                        color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                        font.pixelSize: shell.metrics.bodyFont
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: filterTap
                        anchors.fill: parent
                        onClicked: { shell.view.setAvailabilityFilter(index); root.filterMenuOpen = false }
                    }
                }
            }
        }
    }

    Rectangle {
        id: contextMenu
        objectName: "nativeContextMenu"
        z: 22
        visible: root.contextOpen
        anchors { right: parent.right; top: parent.top; topMargin: shell.metrics.bookRow }
        width: Math.min(parent.width * 0.7, 662 * shell.metrics.scale)
        height: contextItems.height + 2
        color: "white"
        border.color: "black"
        Column {
            id: contextItems
            width: parent.width - 2
            anchors.centerIn: parent
            Repeater {
                model: ["Book info · " + root.contextTitle, "Cancel"]
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: contextItems.width
                    height: shell.metrics.menuRow
                    color: contextTap.pressed ? "black" : "white"
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "black" }
                    Text {
                        anchors { fill: parent; leftMargin: shell.metrics.gap; rightMargin: shell.metrics.gap }
                        text: modelData
                        color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                        font.pixelSize: shell.metrics.bodyFont
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: contextTap
                        anchors.fill: parent
                        onClicked: {
                            root.contextOpen = false
                            if (index === 0) root.openBook(root.contextAccount, root.contextHash)
                        }
                    }
                }
            }
        }
    }
}
