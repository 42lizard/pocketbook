import QtQuick
import com.pocketbook.controls

Flickable {
    id: root
    objectName: "nativeSignInPage"
    required property var shell
    contentHeight: form.height
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    Column {
        id: form
        width: root.width
        spacing: shell.metrics.smallGap
        Text { width: parent.width; text: "SIGN IN TO READEST"; font.pixelSize: shell.metrics.titleFont; font.bold: true }
        Text { width: parent.width; text: "Email"; font.pixelSize: shell.metrics.smallFont }
        FramedTextInput {
            id: email
            objectName: "emailInput"
            width: parent.width; height: shell.metrics.framedInput
            placeholderText: "name@example.com"
            font.pixelSize: shell.metrics.bodyFont
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText
        }
        Text { width: parent.width; text: "Password"; font.pixelSize: shell.metrics.smallFont }
        FramedTextInput {
            id: password
            objectName: "passwordInput"
            width: parent.width; height: shell.metrics.framedInput
            placeholderText: "Password"
            font.pixelSize: shell.metrics.bodyFont
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
        }
        MessageBand { width: parent.width; shell: root.shell }
        Rectangle {
            width: parent.width
            height: shell.metrics.primaryAction
            color: signInTap.pressed ? "white" : "black"
            border.color: "black"
            Text {
                anchors.fill: parent
                text: "Sign in"
            color: Qt.colorEqual(parent.color, "black") ? "white" : "black"
                font.pixelSize: shell.metrics.bodyFont
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea {
                id: signInTap
                anchors.fill: parent
                onClicked: { let secret = password.text; password.text = ""; shell.view.signIn(email.text, secret) }
            }
        }
        Text {
            width: parent.width
            text: "Downloaded books remain available without signing in."
            font.pixelSize: shell.metrics.smallFont
            wrapMode: Text.Wrap
        }
    }
}
