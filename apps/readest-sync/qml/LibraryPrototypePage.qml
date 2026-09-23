// THROWAWAY PROTOTYPE. Three Readest Sync library variants switchable from the bottom bar.
// Run from the repository root: docker compose up -d --build simulator
import QtQuick

Item {
    id: root
    required property var shell
    property int variant: 0
    property bool menuOpen: false
    property var variantNames: ["A · Detailed list", "B · Cover shelf", "C · Continue focus"]

    Rectangle {
        anchors.fill: parent
        color: "white"
    }

    Row {
        id: libraryTools
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: shell.buttonHeight
        spacing: shell.gap

        BodyText {
            width: parent.width - menuButton.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: root.variant === 0 ? "Library" : root.variant === 1 ? "All books" : "Continue reading"
            font.bold: true
            font.pixelSize: Math.max(22, shell.width / 42)
            elide: Text.ElideRight
        }
        Rectangle {
            id: menuButton
            objectName: "prototypeLibraryMenu"
            width: shell.buttonHeight * 1.8
            height: shell.buttonHeight
            color: menuTap.pressed || root.menuOpen ? "black" : "white"
            border.color: "black"
            BodyText { anchors.centerIn: parent; text: "Menu"; color: parent.color === "#000000" ? "white" : "black" }
            MouseArea { id: menuTap; anchors.fill: parent; onClicked: root.menuOpen = !root.menuOpen }
        }
    }

    Rectangle {
        id: filterLine
        anchors { top: libraryTools.bottom; left: parent.left; right: parent.right; topMargin: shell.gap / 2 }
        height: shell.buttonHeight * 0.8
        color: "#eeeeee"
        BodyText {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: shell.gap / 2 }
            text: "Filter: All books"
        }
        BodyText {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: shell.gap / 2 }
            text: "×"
            font.pixelSize: Math.max(24, shell.width / 36)
        }
    }

    Loader {
        id: variantLoader
        anchors { top: filterLine.bottom; left: parent.left; right: parent.right; bottom: pageInfo.top; topMargin: shell.gap; bottomMargin: shell.gap / 2 }
        sourceComponent: root.variant === 0 ? detailedVariant : root.variant === 1 ? coversVariant : focusVariant
    }

    BodyText {
        id: pageInfo
        anchors { left: parent.left; right: parent.right; bottom: prototypeSwitcher.top; bottomMargin: shell.gap / 3 }
        height: shell.buttonHeight * 0.65
        text: "Page " + (shell.view.library.page || 1) + " of " + (shell.view.library.pages || 1)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Rectangle {
        id: prototypeSwitcher
        objectName: "prototypeSwitcher"
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
        width: Math.min(parent.width, shell.buttonHeight * 8.5)
        height: shell.buttonHeight
        color: "black"
        z: 20
        Row {
            anchors.fill: parent
            Rectangle {
                width: parent.width * 0.2; height: parent.height; color: previousTap.pressed ? "#555555" : "black"
                BodyText { anchors.centerIn: parent; text: "‹"; color: "white"; font.pixelSize: Math.max(30, shell.width / 30) }
                MouseArea { id: previousTap; anchors.fill: parent; onClicked: root.variant = (root.variant + 2) % 3 }
            }
            BodyText {
                width: parent.width * 0.6; height: parent.height
                text: root.variantNames[root.variant]
                color: "white"; font.bold: true
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            }
            Rectangle {
                width: parent.width * 0.2; height: parent.height; color: nextTap.pressed ? "#555555" : "black"
                BodyText { anchors.centerIn: parent; text: "›"; color: "white"; font.pixelSize: Math.max(30, shell.width / 30) }
                MouseArea { id: nextTap; anchors.fill: parent; onClicked: root.variant = (root.variant + 1) % 3 }
            }
        }
    }

    Rectangle {
        id: menuPanel
        visible: root.menuOpen
        z: 30
        anchors { top: libraryTools.bottom; right: parent.right }
        width: Math.min(parent.width * 0.68, shell.buttonHeight * 7)
        height: menuColumn.height + shell.gap
        color: "white"
        border.color: "black"
        Column {
            id: menuColumn
            anchors { top: parent.top; left: parent.left; right: parent.right; margins: shell.gap / 2 }
            Repeater {
                model: ["Search", "Filter by availability", "Refresh library", "Scan device", shell.view.signedIn ? "Account · Sign out" : "Sign in to Readest"]
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: menuColumn.width
                    height: shell.buttonHeight
                    color: itemTap.pressed ? "black" : "white"
                    BodyText {
                        anchors { fill: parent; leftMargin: shell.gap / 2 }
                        text: modelData
                        color: parent.color === "#000000" ? "white" : "black"
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        id: itemTap
                        anchors.fill: parent
                        onClicked: {
                            root.menuOpen = false
                            if (index === 2) root.shell.view.refreshLibrary()
                            else if (index === 3) root.shell.view.scanDevice()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: detailedVariant
        ListView {
            clip: true
            spacing: 1
            model: root.shell.view.library
            delegate: Rectangle {
                required property string account
                required property string bookHash
                required property string bookTitle
                required property string author
                required property string availability
                required property string coverPath
                required property string localProgress
                width: ListView.view.width
                height: Math.max(root.shell.buttonHeight * 2.25, 138)
                color: rowTap.pressed ? "#eeeeee" : "white"
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: "#bdbdbd"
                }
                Image {
                    id: rowCover
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: root.shell.gap / 3 }
                    width: height * 0.68
                    source: coverPath ? "image://cover/" + encodeURIComponent(coverPath) : ""
                    fillMode: Image.PreserveAspectFit
                }
                Rectangle {
                    anchors.fill: rowCover
                    visible: rowCover.status !== Image.Ready
                    color: "#eeeeee"
                    BodyText { anchors.centerIn: parent; text: "Book"; font.bold: true }
                }
                Column {
                    anchors { left: rowCover.right; right: statusColumn.left; verticalCenter: parent.verticalCenter; leftMargin: root.shell.gap / 2; rightMargin: root.shell.gap / 2 }
                    spacing: root.shell.gap / 4
                    BodyText { width: parent.width; text: bookTitle; font.bold: true; maximumLineCount: 2; elide: Text.ElideRight }
                    BodyText { width: parent.width; text: author; maximumLineCount: 1; elide: Text.ElideRight }
                }
                Column {
                    id: statusColumn
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: root.shell.gap / 3 }
                    width: Math.max(150, parent.width * 0.22)
                    spacing: root.shell.gap / 4
                    BodyText { width: parent.width; text: localProgress; font.bold: true; horizontalAlignment: Text.AlignRight }
                    BodyText { width: parent.width; text: availability; maximumLineCount: 2; horizontalAlignment: Text.AlignRight; font.pixelSize: Math.max(14, root.shell.width / 65) }
                }
                MouseArea { id: rowTap; anchors.fill: parent; onClicked: root.shell.view.selectBook(account, bookHash) }
            }
        }
    }

    Component {
        id: coversVariant
        Grid {
            columns: root.shell.width > root.shell.height ? 4 : 3
            spacing: root.shell.gap
            Repeater {
                model: root.shell.view.library
                delegate: Item {
                    required property string account
                    required property string bookHash
                    required property string bookTitle
                    required property string author
                    required property string availability
                    required property string coverPath
                    required property string localProgress
                    width: (variantLoader.width - (parent.columns - 1) * parent.spacing) / parent.columns
                    height: (variantLoader.height - parent.spacing) / 2
                    Image {
                        id: shelfCover
                        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
                        width: parent.width * 0.72; height: parent.height * 0.68
                        source: coverPath ? "image://cover/" + encodeURIComponent(coverPath) : ""
                        fillMode: Image.PreserveAspectFit
                    }
                    Rectangle { anchors.fill: shelfCover; visible: shelfCover.status !== Image.Ready; color: "#eeeeee"; BodyText { anchors.centerIn: parent; text: "Book"; font.bold: true } }
                    BodyText {
                        anchors { top: shelfCover.bottom; left: parent.left; right: parent.right; topMargin: root.shell.gap / 4 }
                        text: bookTitle
                        font.bold: true
                        maximumLineCount: 1
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                    }
                    BodyText {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        text: localProgress + " · " + availability
                        maximumLineCount: 1
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Math.max(14, root.shell.width / 65)
                    }
                    MouseArea { anchors.fill: parent; onClicked: root.shell.view.selectBook(account, bookHash) }
                }
            }
        }
    }

    Component {
        id: focusVariant
        Column {
            spacing: root.shell.gap
            Repeater {
                model: root.shell.view.library
                delegate: Rectangle {
                    required property int index
                    required property string account
                    required property string bookHash
                    required property string bookTitle
                    required property string author
                    required property string availability
                    required property string coverPath
                    required property string localProgress
                    width: variantLoader.width
                    height: index === 0 ? Math.min(variantLoader.height * 0.48, 420) : Math.max(root.shell.buttonHeight * 1.45, 88)
                    color: focusTap.pressed ? "#eeeeee" : "white"
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 1
                        color: "#bdbdbd"
                    }
                    Image {
                        id: focusCover
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: root.shell.gap / 3 }
                        width: index === 0 ? height * 0.68 : height * 0.6
                        source: coverPath ? "image://cover/" + encodeURIComponent(coverPath) : ""
                        fillMode: Image.PreserveAspectFit
                    }
                    Rectangle { anchors.fill: focusCover; visible: focusCover.status !== Image.Ready; color: "#eeeeee" }
                    Column {
                        anchors { left: focusCover.right; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: root.shell.gap / 2; rightMargin: root.shell.gap / 2 }
                        spacing: root.shell.gap / 4
                        BodyText { width: parent.width; text: bookTitle; font.bold: true; font.pixelSize: index === 0 ? Math.max(24, root.shell.width / 34) : Math.max(18, root.shell.width / 43); maximumLineCount: index === 0 ? 3 : 1; elide: Text.ElideRight }
                        BodyText { width: parent.width; text: author; maximumLineCount: 1; elide: Text.ElideRight }
                        BodyText { width: parent.width; text: localProgress + " · " + availability; font.bold: index === 0; maximumLineCount: 1; elide: Text.ElideRight }
                        BodyText { visible: index === 0; width: parent.width; text: "Tap to continue reading"; font.pixelSize: Math.max(14, root.shell.width / 65) }
                    }
                    MouseArea { id: focusTap; anchors.fill: parent; onClicked: root.shell.view.selectBook(account, bookHash) }
                }
            }
        }
    }
}
