import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

SettingsPage {
    id:         root
    objectName: "settingsPage_DeveloperMode"

    readonly property int  _tapsToUnlock:   7
    // 7인치 1280x800 에서 1mm = 8.49px. 터치 최소 7mm = 59px 를 바닥으로 깐다.
    // main 이 이 화면 크기의 기본 글꼴을 12pt 로 낮췄기 때문에 비율만으로는 부족하다.
    readonly property real _touchHeight:    Math.max(59, ScreenTools.defaultFontPixelHeight * 2.5)

    property bool   _developerMode:   QGroundControl.corePlugin.showAdvancedUI
    property bool   _pinPrompt:     false
    property bool   _pinChangeOpen: false
    property int    _tapCount:      0
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

    // The PIN, its hash and the lockout live in DeveloperPin; this page only asks, and opens
    // developer mode on a yes.
    function _submitPin() {
        if (App.DeveloperPin.verify(pinField.text)) {
            pinField.text = ""
            _pinPrompt = false
            _pinMessage = ""
            QGroundControl.corePlugin.showAdvancedUI = true
        } else {
            pinField.text = ""
            _pinMessage = App.DeveloperPin.lockedOut ? "" : qsTr("PIN이 틀렸습니다.")
        }
    }

    function _submitPinChange() {
        if (newPinField.text !== confirmPinField.text) {
            _pinMessage = qsTr("새 PIN이 서로 다릅니다.")
            return
        }
        if (App.DeveloperPin.changePin(currentPinField.text, newPinField.text)) {
            currentPinField.text = ""
            newPinField.text = ""
            confirmPinField.text = ""
            _pinChangeOpen = false
            _pinMessage = qsTr("개발자 PIN을 바꿨습니다.")
        } else {
            _pinMessage = qsTr("현재 PIN이 틀렸거나 새 PIN이 맞지 않습니다. 숫자 4~8자리이고, 출고 초기값은 쓸 수 없습니다.")
        }
    }

    function _turnOff() {
        _pinChangeOpen = false
        _pinMessage = ""
        QGroundControl.corePlugin.showAdvancedUI = false
    }

    Timer {
        id:         tapWindowTimer
        interval:   5000
        onTriggered: root._tapCount = 0
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
                verticalAlignment:      Text.AlignVCenter
                text:                   QGroundControl.qgcVersion

                QGCMouseArea {
                    fillItem:   parent
                    // The row is drawn at the settings rows' height; the tap target keeps _touchHeight
                    anchors.topMargin:      -Math.max(0, root._touchHeight - parent.height) / 2
                    anchors.bottomMargin:   -Math.max(0, root._touchHeight - parent.height) / 2
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

        RowLayout {
            Layout.fillWidth:   true
            visible:            root._developerMode
            spacing:            ScreenTools.defaultFontPixelWidth * 2

            QGCButton {
                // Drawn at the settings rows' button size, while the button itself, and so its touch
                // target, keeps _touchHeight and reaches into the gaps above and below its row
                Layout.preferredHeight: root._touchHeight
                Layout.topMargin:       -_touchReach
                Layout.bottomMargin:    -_touchReach
                topInset:               _touchReach
                bottomInset:            _touchReach
                text:                   qsTr("Turn Off Developer Mode")

                readonly property real _touchReach: Math.max(0, root._touchHeight - (implicitContentHeight + topPadding + bottomPadding)) / 2

                onClicked:              root._turnOff()
            }

            QGCButton {
                Layout.preferredHeight: root._touchHeight
                Layout.topMargin:       -_touchReach
                Layout.bottomMargin:    -_touchReach
                topInset:               _touchReach
                bottomInset:            _touchReach
                text:                   qsTr("PIN 변경")

                readonly property real _touchReach: Math.max(0, root._touchHeight - (implicitContentHeight + topPadding + bottomPadding)) / 2

                onClicked: {
                    root._pinChangeOpen = !root._pinChangeOpen
                    root._pinMessage = ""
                }
            }
        }

        // Said until it is no longer true: the value a unit shipped with is in the package, and
        // a PIN that nobody changed is a lock in name.
        QGCLabel {
            Layout.fillWidth:   true
            visible:            root._developerMode && App.DeveloperPin.pinIsDefault
            wrapMode:           Text.WordWrap
            color:              qgcPal.colorOrange
            text:               qsTr("개발자 PIN이 출고 초기값 그대로입니다. 바꾸십시오.")
        }

        RowLayout {
            Layout.fillWidth:   true
            visible:            root._developerMode && root._pinChangeOpen
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCLabel { text: qsTr("현재") }
            QGCTextField {
                id:                     currentPinField
                Layout.fillWidth:       true
                Layout.preferredHeight: root._touchHeight
                echoMode:               TextInput.Password
                maximumLength:          8
                numericValuesOnly:      true
            }
            QGCLabel { text: qsTr("새 PIN") }
            QGCTextField {
                id:                     newPinField
                Layout.fillWidth:       true
                Layout.preferredHeight: root._touchHeight
                echoMode:               TextInput.Password
                maximumLength:          8
                numericValuesOnly:      true
            }
            QGCLabel { text: qsTr("확인") }
            QGCTextField {
                id:                     confirmPinField
                Layout.fillWidth:       true
                Layout.preferredHeight: root._touchHeight
                echoMode:               TextInput.Password
                maximumLength:          8
                numericValuesOnly:      true
                onAccepted:             root._submitPinChange()
            }
            QGCButton {
                Layout.preferredHeight: root._touchHeight
                text:                   qsTr("변경")
                onClicked:              root._submitPinChange()
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            root._developerMode && text !== ""
            text:               root._pinMessage
            wrapMode:           Text.WordWrap
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
                maximumLength:          8
                numericValuesOnly:      true
                enabled:                !App.DeveloperPin.lockedOut
                onAccepted:             root._submitPin()

                // 안드로이드에서 화면 키패드가 바로 뜨게 한다
                onVisibleChanged:       if (visible) forceActiveFocus()
            }

            QGCButton {
                Layout.preferredHeight: root._touchHeight
                text:                   qsTr("Unlock")
                enabled:                !App.DeveloperPin.lockedOut
                onClicked:              root._submitPin()
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            text !== ""
            wrapMode:           Text.WordWrap
            text:               App.DeveloperPin.lockedOut
                                    ? qsTr("PIN을 여러 번 틀렸습니다. %1초 후 다시 시도하십시오.").arg(App.DeveloperPin.lockoutSecondsLeft)
                                    : root._pinMessage
        }
    }
}
