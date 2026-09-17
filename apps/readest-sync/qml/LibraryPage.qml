import QtQuick
import com.pocketbook.controls

Item {
    required property var shell
    Column {
        id: toolbar
        width: parent.width
        spacing: shell.gap
        Row {
            width: parent.width; spacing: shell.gap
            ActionButton { width: (parent.width - shell.gap) / 2; text: "Refresh library"; onAction: shell.view.refreshLibrary() }
            ActionButton { width: (parent.width - shell.gap) / 2; text: "Scan device"; onAction: shell.view.scanDevice() }
        }
        FramedTextInput {
            id: search
            objectName: "searchInput"
            width: parent.width; height: shell.buttonHeight
            placeholderText: "Search title or author"
            text: shell.view.library.searchText || ""
            onTextEdited: shell.view.search(text)
        }
        Row {
            width: parent.width; spacing: shell.gap / 2
            Repeater {
                model: ["All books", "Available to download", "On device", "Progress only", "PocketBook only"]
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: (toolbar.width - 4 * shell.gap / 2) / 5
                    height: shell.buttonHeight
                    color: shell.view.library.availabilityFilter === index ? "black" : "white"
                    border.color: "black"
                    Text {
                        anchors { fill: parent; margins: 3 }
                        text: modelData; color: parent.color == "#000000" ? "white" : "black"
                        font.pixelSize: Math.max(14, shell.width / 65)
                        wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea { anchors.fill: parent; onClicked: shell.view.setAvailabilityFilter(index) }
                }
            }
        }
    }
    Grid {
        id: libraryGrid
        objectName: "libraryGrid"
        anchors { top: toolbar.bottom; topMargin: shell.gap; left: parent.left; right: parent.right; bottom: footer.top; bottomMargin: shell.gap }
        columns: shell.width > shell.height ? 4 : 3
        rows: shell.width > shell.height ? 1 : 2
        spacing: shell.gap
        Repeater {
            model: shell.view.library
            delegate: Rectangle {
                id: tile
                required property string account
                required property string bookHash
                required property string bookTitle
                required property string author
                required property string availability
                required property string coverPath
                required property string localProgress
                required property string readestProgress
                width: (libraryGrid.width - (libraryGrid.columns - 1) * libraryGrid.spacing) / libraryGrid.columns
                height: (libraryGrid.height - (libraryGrid.rows - 1) * libraryGrid.spacing) / libraryGrid.rows
                color: tap.pressed ? "#dddddd" : "white"
                border.color: "black"
                clip: true
                Column {
                    anchors { fill: parent; margins: shell.gap / 2 }
                    spacing: shell.gap / 3
                    Item {
                        width: parent.width; height: Math.max(30, parent.height - shell.buttonHeight * 2 - progressText.height - 4 * parent.spacing)
                        Image {
                            id: cover
                            anchors.fill: parent
                            source: tile.coverPath ? "image://cover/" + encodeURIComponent(tile.coverPath) : ""
                            sourceSize.width: Math.min(1024, width)
                            sourceSize.height: Math.min(1024, height)
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                        }
                        BodyText {
                            anchors.fill: parent
                            visible: cover.status !== Image.Ready
                            text: tile.bookTitle
                            font.pixelSize: Math.max(16, shell.width / 55)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    BodyText { width: parent.width; height: shell.buttonHeight; maximumLineCount: 2; elide: Text.ElideRight; text: tile.bookTitle; font.bold: true; font.pixelSize: Math.max(16, shell.width / 55) }
                    BodyText { width: parent.width; height: shell.buttonHeight / 2; elide: Text.ElideRight; maximumLineCount: 1; text: tile.author; font.pixelSize: Math.max(14, shell.width / 65) }
                    Rectangle {
                        width: parent.width; height: shell.buttonHeight / 2; color: "#dddddd"
                        Text { anchors.fill: parent; text: tile.availability; color: "black"; font.pixelSize: Math.max(14, shell.width / 65); horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    }
                    BodyText {
                        id: progressText
                        objectName: "bookProgress"
                        width: parent.width
                        text: "PocketBook: " + tile.localProgress + "\nReadest: " + tile.readestProgress
                        font.pixelSize: Math.max(14, shell.width / 65)
                        maximumLineCount: 2
                    }
                }
                MouseArea { id: tap; anchors.fill: parent; onClicked: shell.view.selectBook(tile.account, tile.bookHash) }
            }
        }
    }
    BodyText {
        anchors.centerIn: libraryGrid
        visible: shell.view.library.count === 0
        text: "No books. Refresh your library or change the search."
        width: libraryGrid.width
        horizontalAlignment: Text.AlignHCenter
    }
    Column {
        id: footer
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        spacing: shell.gap / 2
        Row {
            width: parent.width; spacing: shell.gap
            ActionButton { width: (parent.width-shell.gap)/2; text: "Previous"; enabled: shell.view.library.page > 1; onAction: shell.view.turnPage(-1) }
            ActionButton { width: (parent.width-shell.gap)/2; text: "Next"; enabled: shell.view.library.page < shell.view.library.pages; onAction: shell.view.turnPage(1) }
        }
        BodyText { width: parent.width; text: (shell.view.library.count || 0) + " books · Page " + (shell.view.library.page || 1) + " / " + (shell.view.library.pages || 1); horizontalAlignment: Text.AlignHCenter }
        BodyText { width: parent.width; text: shell.view.status || ""; font.pixelSize: Math.max(14, shell.width / 65); maximumLineCount: 3; elide: Text.ElideRight }
        ActionButton { width: parent.width; text: shell.view.signedIn ? "Sign out" : "Sign in to Readest"; onAction: shell.view.signedIn ? shell.view.signOut() : shell.view.showSignIn() }
    }
}
