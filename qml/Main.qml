import QtQuick
import QtQuick.Controls
import QtQuick.Window

import AOWIS.ServerGui

Window {
    width: 400
    height: 240
    visible: true
    title: "AOWIS-SERVER-GUI"

    Column {
        anchors.centerIn: parent
        spacing: 12

        Text {
            text: "Hello QML"
            font.pixelSize: 24
        }

        Button {
            text: "Click me"
            onClicked: console.log("Button clicked")
        }
        
        MapViewer {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            height: 400
        }
    }
}
