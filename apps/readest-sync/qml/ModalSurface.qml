import QtQuick

Item {
    Rectangle { anchors.fill: parent; z: -2; color: "#dddddd"; opacity: 0.92 }
    // Content remains above the input shield; background taps never reach pages.
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = true }
    }
}
