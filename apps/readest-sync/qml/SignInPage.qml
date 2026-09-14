import QtQuick
import com.pocketbook.controls

Column {
    required property var shell
    spacing: shell.gap
    BodyText { width: parent.width; text: "Sign in to your Readest library" }
    BodyText { width: parent.width; visible: text.length > 0; text: shell.signInNotice }
    FramedTextInput {
        id: email
        objectName: "emailInput"
        width: parent.width; height: shell.buttonHeight
        placeholderText: "Email"
        inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText
    }
    FramedTextInput {
        id: password
        objectName: "passwordInput"
        width: parent.width; height: shell.buttonHeight
        placeholderText: "Password"
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
    }
    ActionButton {
        width: parent.width; text: "Sign in"
        onAction: { var secret = password.text; password.text = ""; shell.view.signIn(email.text, secret) }
    }
    BodyText { width: parent.width; text: shell.view.status || "" }
}
