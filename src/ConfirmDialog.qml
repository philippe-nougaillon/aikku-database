import QtQuick
import QtQuick.Controls

// Confirmation dialog over the dimmed window, answered from the keyboard or mouse.
Rectangle {
    id: dialog
    anchors.fill: parent
    color: "#cc000000"
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    readonly property int forward: LayoutMirroring.enabled ? -1 : 1

    property string title: ""
    property string message: ""
    property var buttons: []
    property int initialFocus: 0
    signal chosen(int index)
    signal dismissed()

    function focusButton(index) {
        var count = buttonRepeater.count
        if (count > 0)
            buttonRepeater.itemAt((index + count) % count).forceActiveFocus()
    }
    onVisibleChanged: if (visible) focusButton(initialFocus)

    MouseArea {
        anchors.fill: parent
        onClicked: dialog.dismissed()
    }

    DialogPanel {
        anchors.centerIn: parent
        width: column.width + 64
        height: column.height + 48

        MouseArea { anchors.fill: parent }

        Column {
            id: column
            anchors.centerIn: parent
            spacing: 8

            Label {
                text: dialog.title
                color: "white"
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Label {
                width: Math.min(implicitWidth, dialog.width - 160)
                text: dialog.message
                color: "#d6d6da"
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                bottomPadding: 12
            }
            Row {
                anchors.right: parent.right
                spacing: 10
                Repeater {
                    id: buttonRepeater
                    model: dialog.buttons
                    DialogButton {
                        required property var modelData
                        required property int index
                        text: modelData.text
                        mnemonic: modelData.mnemonic || ""
                        primary: modelData.primary === true
                        onClicked: dialog.chosen(index)
                        Keys.onLeftPressed: dialog.focusButton(index - dialog.forward)
                        Keys.onRightPressed: dialog.focusButton(index + dialog.forward)
                        Keys.onBacktabPressed: dialog.focusButton(index - 1)
                        Keys.onTabPressed: dialog.focusButton(index + 1)
                    }
                }
            }
        }
    }
}
