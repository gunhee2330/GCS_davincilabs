import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

SettingsPage {
    id:         root
    objectName: "settingsPage_ServiceMode"

    // TODO: Move to the Android Keystore before shipping and derive it from the
    // vehicle serial number. This literal is a development placeholder only: QML
    // is compiled into the package, so the value is recoverable from the binary.
    // The failure counter and the lockout below live on this page instance, so
    // leaving the page and coming back clears them. Move that state out at the
    // same time as the PIN.
    readonly property string _servicePin: "704183"

    readonly property int  _tapsToUnlock:   7
    readonly property int  _maxPinFailures: 5
    readonly property real _touchHeight:    ScreenTools.defaultFontPixelHeight * 2.5

    property bool   _serviceMode:   QGroundControl.corePlugin.showAdvancedUI
    property bool   _pinPrompt:     false
    property int    _tapCount:      0
    property int    _failCount:     0
    property string _pinMessage:    ""

    function _registerTap() {
        if (!tapWindowTimer.running) {
            _tapCount = 0
        }
        tapWindowTimer.restart()
        _tapCount++
        if (_tapCount >= _tapsToUnlock) {
            _tapCount = 0
            tapWindowTimer.stop()
            _pinMessage = ""
            _pinPrompt = true
        }
    }

    function _submitPin() {
        if (lockoutTimer.running) {
            return
        }
        if (pinField.text === _servicePin) {
            pinField.text = ""
            _failCount = 0
            _pinPrompt = false
            _pinMessage = ""
            QGroundControl.corePlugin.showAdvancedUI = true
        } else {
            pinField.text = ""
            _failCount++
            if (_failCount >= _maxPinFailures) {
                _failCount = 0
                lockoutTimer.restart()
                _pinMessage = qsTr("Too many incorrect attempts. Locked for 60 seconds.")
            } else {
                _pinMessage = qsTr("Incorrect PIN.")
            }
        }
    }

    Timer {
        id:         tapWindowTimer
        interval:   5000
        onTriggered: root._tapCount = 0
    }

    Timer {
        id:         lockoutTimer
        interval:   60000
        onTriggered: root._pinMessage = ""
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Service Mode")

        LabelledLabel {
            Layout.fillWidth:   true
            label:              qsTr("Status")
            labelText:          root._serviceMode ? qsTr("Enabled") : qsTr("Disabled")
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth * 2

            QGCLabel {
                Layout.fillWidth:   true
                text:               qsTr("Application Version")
            }

            QGCLabel {
                Layout.preferredHeight: root._touchHeight
                verticalAlignment:      Text.AlignVCenter
                text:                   QGroundControl.qgcVersion

                QGCMouseArea {
                    fillItem:   parent
                    enabled:    !root._serviceMode
                    onClicked:  root._registerTap()
                }
            }
        }

        LabelledLabel {
            Layout.fillWidth:   true
            label:              qsTr("Build Date")
            labelText:          QGroundControl.qgcAppDate
        }

        QGCButton {
            Layout.preferredHeight: root._touchHeight
            text:                   qsTr("Turn Off Service Mode")
            visible:                root._serviceMode
            onClicked:              QGroundControl.corePlugin.showAdvancedUI = false
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Enter Service PIN")
        visible:            root._pinPrompt && !root._serviceMode

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth * 2

            QGCTextField {
                id:                     pinField
                Layout.fillWidth:       true
                Layout.preferredHeight: root._touchHeight
                echoMode:               TextInput.Password
                maximumLength:          6
                numericValuesOnly:      true
                enabled:                !lockoutTimer.running
                onAccepted:             root._submitPin()

                // 안드로이드에서 화면 키패드가 바로 뜨게 한다
                onVisibleChanged:       if (visible) forceActiveFocus()
            }

            QGCButton {
                Layout.preferredHeight: root._touchHeight
                text:                   qsTr("Unlock")
                enabled:                !lockoutTimer.running
                onClicked:              root._submitPin()
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               root._pinMessage
            visible:            text !== ""
            wrapMode:           Text.WordWrap
        }
    }
}
