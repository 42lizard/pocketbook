import QtQuick

Item {
    id: root
    objectName: "nativeBookDetailsPage"
    required property var shell
    property var details: shell.view.book || ({})
    property var availableActions: shell.view.actions || []
    property bool menuOpen: false
    property bool activating: false
    readonly property bool choosing: availableActions.length > 0 &&
        (availableActions[0].command.indexOf("copy:") === 0 ||
         availableActions[0].command.indexOf("use") === 0 ||
         availableActions[0].command.indexOf("openPocketBook") === 0)
    readonly property var primaryAction: choosing ? null : firstContentAction()
    readonly property var secondaryActions: contentActions(false)
    readonly property var menuActions: contentActions(true)
    readonly property bool hasMenu: menuActions.length > 0

    function firstContentAction() {
        for (let action of availableActions)
            if (action.command !== "back" && action.command !== "uploadCover") return action
        return null
    }
    function contentActions(menu) {
        let result = []
        for (let action of availableActions) {
            if (action.command === "back") continue
            if (menu) {
                if (action.command === "uploadCover") result.push(action)
            } else if (choosing || action.command !== (primaryAction ? primaryAction.command : "")) {
                if (action.command !== "uploadCover") result.push(action)
            }
        }
        return result
    }
    function run(command) {
        if (activating) return
        activating = true
        menuOpen = false
        shell.view.runAction(command)
        activationGuard.restart()
    }
    function toggleMenu() { if (hasMenu) menuOpen = !menuOpen }
    function handleKey(key) {
        if (menuOpen && (key === Qt.Key_Back || key === Qt.Key_Escape || key === Qt.Key_Home)) {
            menuOpen = false
            return true
        }
        return false
    }

    Timer { id: activationGuard; interval: 600; onTriggered: root.activating = false }

    Flickable {
        id: scroll
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: actionArea.top }
        contentHeight: content.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: scroll.width

            Item {
                width: parent.width
                height: 330 * shell.metrics.scale
                Image {
                    id: detailCover
                    anchors { left: parent.left; top: parent.top; margins: shell.metrics.largeGap }
                    width: 175 * shell.metrics.scale
                    height: 258 * shell.metrics.scale
                    source: root.details.coverPath ? "image://cover/" + encodeURIComponent(root.details.coverPath) : ""
                    sourceSize.width: Math.min(1024, width)
                    sourceSize.height: Math.min(1024, height)
                    fillMode: Image.PreserveAspectFit
                }
                Rectangle {
                    anchors.fill: detailCover
                    visible: detailCover.status !== Image.Ready
                    color: "#eeeeee"
                    border.color: "black"
                    Text { anchors.centerIn: parent; text: "Book"; font.pixelSize: shell.metrics.smallFont }
                }
                Column {
                    anchors { left: detailCover.right; right: parent.right; top: parent.top; margins: shell.metrics.largeGap }
                    spacing: shell.metrics.smallGap
                    Text {
                        width: parent.width
                        text: root.details.title || "Untitled"
                        color: "black"
                        font.pixelSize: shell.metrics.titleFont
                        font.bold: true
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: root.details.author || "Unknown author"
                        color: "black"
                        font.pixelSize: shell.metrics.bodyFont
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: root.details.availability || ""
                        color: "black"
                        font.pixelSize: shell.metrics.metaFont
                        wrapMode: Text.Wrap
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: "black" }
            Text {
                width: parent.width - 2 * shell.metrics.largeGap
                height: shell.metrics.menuRow * 0.7
                x: shell.metrics.largeGap
                text: "READING PROGRESS"
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: shell.metrics.smallFont
                font.bold: true
            }
            DetailRow { width: parent.width; label: "PocketBook"; value: root.details.pocketBookProgress || "—"; metrics: shell.metrics }
            DetailRow { width: parent.width; label: "Readest"; value: root.details.readestProgress || "—"; metrics: shell.metrics }
            Text {
                width: parent.width - 2 * shell.metrics.largeGap
                x: shell.metrics.largeGap
                topPadding: shell.metrics.smallGap
                bottomPadding: shell.metrics.gap
                text: "Percentages use each reader’s page counts."
                color: "black"
                font.pixelSize: shell.metrics.smallFont
                wrapMode: Text.Wrap
            }

            Rectangle {
                width: parent.width
                height: statusText.implicitHeight + 2 * shell.metrics.smallGap
                visible: statusText.text.length > 0
                color: "white"
                border.color: "black"
                Text {
                    id: statusText
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: shell.metrics.largeGap }
                    text: shell.view.status || ""
                    color: "black"
                    font.pixelSize: shell.metrics.smallFont
                    wrapMode: Text.Wrap
                }
            }

            Text {
                visible: root.choosing
                width: parent.width - 2 * shell.metrics.largeGap
                x: shell.metrics.largeGap
                height: shell.metrics.menuRow
                text: root.availableActions[0] && root.availableActions[0].command.indexOf("copy:") === 0 ?
                    "CHOOSE LOCAL COPY" : "CHOOSE READING POSITION"
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: shell.metrics.smallFont
                font.bold: true
            }
            Repeater {
                model: root.secondaryActions
                delegate: Rectangle {
                    required property var modelData
                    width: content.width
                    height: shell.metrics.primaryAction
                    color: actionTap.pressed ? "black" : "white"
                    border.color: "black"
                    Text {
                        anchors { fill: parent; margins: shell.metrics.largeGap }
                        text: modelData.text
                        color: parent.color === "#000000" ? "white" : "black"
                        font.pixelSize: shell.metrics.bodyFont
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.Wrap
                    }
                    MouseArea { id: actionTap; anchors.fill: parent; onClicked: root.run(modelData.command) }
                }
            }
        }
    }

    Rectangle {
        id: actionArea
        objectName: "nativePrimaryAction"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: root.primaryAction ? shell.metrics.primaryAction : 0
        visible: root.primaryAction !== null
        color: primaryTap.pressed ? "white" : "black"
        border.color: "black"
        Text {
            anchors.fill: parent
            text: root.primaryAction ? root.primaryAction.text : ""
            color: parent.color === "#000000" ? "white" : "black"
            font.pixelSize: shell.metrics.bodyFont
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
        MouseArea { id: primaryTap; anchors.fill: parent; onClicked: if (root.primaryAction) root.run(root.primaryAction.command) }
    }

    Rectangle {
        objectName: "nativeBookMenu"
        visible: root.menuOpen
        z: 10
        anchors { top: parent.top; right: parent.right; topMargin: shell.metrics.smallGap; rightMargin: shell.metrics.smallGap }
        width: Math.min(parent.width * 0.7, 820 * shell.metrics.scale)
        height: menuColumn.height
        color: "white"
        border { color: "black"; width: Math.max(2, 10 * shell.metrics.scale) }
        Column {
            id: menuColumn
            width: parent.width
            Repeater {
                model: root.menuActions
                delegate: Rectangle {
                    required property var modelData
                    width: menuColumn.width
                    height: shell.metrics.menuRow
                    color: menuTap.pressed ? "black" : "white"
                    Text {
                        anchors { fill: parent; margins: shell.metrics.largeGap }
                        text: modelData.text
                        color: parent.color === "#000000" ? "white" : "black"
                        font.pixelSize: shell.metrics.bodyFont
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea { id: menuTap; anchors.fill: parent; onClicked: root.run(modelData.command) }
                }
            }
        }
    }

    component DetailRow: Rectangle {
        required property string label
        required property string value
        required property var metrics
        height: metrics.menuRow
        color: "white"
        border.color: "black"
        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: metrics.largeGap }
            width: parent.width * 0.55
            text: label
            font.pixelSize: metrics.bodyFont
            font.bold: true
        }
        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: metrics.largeGap }
            width: parent.width * 0.35
            text: value
            font.pixelSize: metrics.bodyFont
            horizontalAlignment: Text.AlignRight
        }
    }
}
