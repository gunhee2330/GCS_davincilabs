import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

SettingsPage {
    id:         root
    objectName: "settingsPage_DeveloperMode"

    // TODO: Move to the Android Keystore before shipping and derive it from the
    // vehicle serial number. This literal is a development placeholder only: QML
    // is compiled into the package, so the value is recoverable from the binary.
    // The failure counter and the lockout below live on this page instance, so
    // leaving the page and coming back clears them. Move that state out at the
    // same time as the PIN.
    readonly property string _developerPin: "704183"

    readonly property int  _tapsToUnlock:   7
    readonly property int  _maxPinFailures: 5
    // 7인치 1280x800 에서 1mm = 8.49px. 터치 최소 7mm = 59px 를 바닥으로 깐다.
    // main 이 이 화면 크기의 기본 글꼴을 12pt 로 낮췄기 때문에 비율만으로는 부족하다.
    readonly property real _touchHeight:    Math.max(59, ScreenTools.defaultFontPixelHeight * 2.5)

    property bool   _developerMode:   QGroundControl.corePlugin.showAdvancedUI
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
        if (pinField.text === _developerPin) {
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
        heading:            qsTr("Developer Mode")

        LabelledLabel {
            Layout.fillWidth:   true
            label:              qsTr("Status")
            labelText:          root._developerMode ? qsTr("Enabled") : qsTr("Disabled")
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
                    enabled:    !root._developerMode
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
            text:                   qsTr("Turn Off Developer Mode")
            visible:                root._developerMode
            onClicked:              QGroundControl.corePlugin.showAdvancedUI = false
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Enter Developer PIN")
        visible:            root._pinPrompt && !root._developerMode

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
