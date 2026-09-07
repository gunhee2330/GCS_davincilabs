import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

ColumnLayout {
    id:         root
    spacing:    ScreenTools.defaultFontPixelHeight / 2

    property real _slewRate:    50      ///< Percentage of the gimbal's maximum slew rate.
    property real _buttonWidth: ScreenTools.defaultFontPixelWidth * 8

    readonly property bool _connected:  SiyiCameraController.connected
    readonly property bool _isZT30:     SiyiCameraController.isZT30
    readonly property bool _aiConfigured: QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue
    readonly property bool _aiConnected:  SiyiAiController.connected

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    function _startSlew(yawRate, pitchRate) {
        SiyiCameraController.rotate(yawRate, pitchRate)
    }

    function _stopSlew() {
        SiyiCameraController.stopRotation()
    }

    Component.onDestruction: root._stopSlew()

    RowLayout {
        Layout.fillWidth: true

        QGCLabel {
            text: SiyiCameraController.model !== "" ? SiyiCameraController.model : qsTr("SIYI")
            font.bold: true
        }

        QGCLabel {
            Layout.fillWidth:   true
            horizontalAlignment: Text.AlignRight
            text:               root._connected ? qsTr("연결됨") : qsTr("연결 안 됨")
            color:              root._connected ? qgcPal.colorGreen : qgcPal.colorOrange
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            SiyiCameraController.firmwareVersion !== ""
        text:               SiyiCameraController.firmwareVersion
        font.pointSize:     ScreenTools.smallFontPointSize
        elide:              Text.ElideRight
    }

    QGCButton {
        Layout.fillWidth:   true
        text:               root._connected ? qsTr("재연결") : qsTr("연결")
        onClicked:          SiyiCameraController.start()
    }

    // ------------------------------------------------------------------ Gimbal

    QGCLabel {
        text:       qsTr("짐벌")
        font.bold:  true
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               qsTr("좌우 %1°   상하 %2°")
                                .arg(SiyiCameraController.yawDeg.toFixed(1))
                                .arg(SiyiCameraController.pitchDeg.toFixed(1))
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    GridLayout {
        columns:            3
        Layout.alignment:   Qt.AlignHCenter
        enabled:            root._connected

        Item { Layout.preferredWidth: root._buttonWidth }

        QGCButton {
            Layout.preferredWidth:  root._buttonWidth
            text:                   "▲"
            onPressed:              root._startSlew(0, root._slewRate)
            onReleased:             root._stopSlew()
            onCanceled:             root._stopSlew()
        }

        Item { Layout.preferredWidth: root._buttonWidth }

        QGCButton {
            Layout.preferredWidth:  root._buttonWidth
            text:                   "◀"
            onPressed:              root._startSlew(-root._slewRate, 0)
            onReleased:             root._stopSlew()
            onCanceled:             root._stopSlew()
        }

        QGCButton {
            Layout.preferredWidth:  root._buttonWidth
            text:                   qsTr("중앙")
            onClicked:              SiyiCameraController.center()
        }

        QGCButton {
            Layout.preferredWidth:  root._buttonWidth
            text:                   "▶"
            onPressed:              root._startSlew(root._slewRate, 0)
            onReleased:             root._stopSlew()
            onCanceled:             root._stopSlew()
        }

        Item { Layout.preferredWidth: root._buttonWidth }

        QGCButton {
            Layout.preferredWidth:  root._buttonWidth
            text:                   "▼"
            onPressed:              root._startSlew(0, -root._slewRate)
            onReleased:             root._stopSlew()
            onCanceled:             root._stopSlew()
        }

        Item { Layout.preferredWidth: root._buttonWidth }
    }

    RowLayout {
        Layout.fillWidth:   true
        enabled:            root._connected

        QGCLabel { text: qsTr("모드") }

        QGCComboBox {
            Layout.fillWidth:   true
            model:              [qsTr("고정"), qsTr("추종"), qsTr("FPV")]
            currentIndex:       SiyiCameraController.motionMode
            onActivated:        (index) => SiyiCameraController.setMotionMode(index)
        }
    }

    // ------------------------------------------------------------------ Camera

    QGCLabel {
        text:       qsTr("카메라")
        font.bold:  true
    }

    RowLayout {
        Layout.fillWidth:   true
        enabled:            root._connected

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("줌 −")
            onPressed:          SiyiCameraController.zoom(-1)
            onReleased:         SiyiCameraController.zoom(0)
            onCanceled:         SiyiCameraController.zoom(0)
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("줌 +")
            onPressed:          SiyiCameraController.zoom(1)
            onReleased:         SiyiCameraController.zoom(0)
            onCanceled:         SiyiCameraController.zoom(0)
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("AF")
            onClicked:          SiyiCameraController.autoFocus()
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               qsTr("줌 %1배").arg(SiyiCameraController.zoomMultiple.toFixed(1))
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    RowLayout {
        Layout.fillWidth:   true
        enabled:            root._connected

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("사진")
            onClicked:          SiyiCameraController.takePhoto()
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               SiyiCameraController.recording ? qsTr("녹화중지") : qsTr("녹화")
            onClicked:          SiyiCameraController.toggleRecording()
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               SiyiCameraController.recordingStatusText
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    // ------------------------------------------------------------------ ZT30 only

    QGCLabel {
        text:       qsTr("센서")
        font.bold:  true
        visible:    root._isZT30
    }

    QGCComboBox {
        Layout.fillWidth:   true
        visible:            root._isZT30
        enabled:            root._connected
        // Index maps directly to the SDK's camera image type values.
        model: [
            qsTr("줌 + 열상 PIP / 보조 광각"),
            qsTr("광각 + 열상 PIP / 보조 줌"),
            qsTr("줌 + 광각 PIP / 보조 열상"),
            qsTr("주 줌 / 보조 열상"),
            qsTr("주 줌 / 보조 광각"),
            qsTr("주 광각 / 보조 열상"),
            qsTr("주 광각 / 보조 줌"),
            qsTr("주 열상 / 보조 줌"),
            qsTr("주 열상 / 보조 광각")
        ]
        onActivated: (index) => SiyiCameraController.setCameraImageType(index)
    }

    RowLayout {
        Layout.fillWidth:   true
        visible:            root._isZT30
        enabled:            root._connected

        QGCLabel { text: qsTr("색상표") }

        QGCComboBox {
            Layout.fillWidth:   true
            // Value 1 is unused in the SIYI palette table, hence the explicit value list.
            property var paletteValues: [0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
            model: [
                qsTr("화이트핫"), qsTr("세피아"), qsTr("아이언보우"), qsTr("레인보우"),
                qsTr("나이트"), qsTr("오로라"), qsTr("레드핫"), qsTr("정글"),
                qsTr("메디컬"), qsTr("블랙핫"), qsTr("글로리핫")
            ]
            onActivated: (index) => SiyiCameraController.setThermalPalette(paletteValues[index])
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            root._isZT30
        text:               SiyiCameraController.thermalRangeAvailable
                                ? qsTr("열상 %1 °C … %2 °C")
                                    .arg(SiyiCameraController.thermalMinTempC.toFixed(1))
                                    .arg(SiyiCameraController.thermalMaxTempC.toFixed(1))
                                : qsTr("열상 데이터 없음")
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            root._isZT30
        text:               SiyiCameraController.rangefinderAvailable
                                ? qsTr("LRF %1 m").arg(SiyiCameraController.rangefinderDistance.toFixed(0))
                                : qsTr("LRF --")
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    // ------------------------------------------------------------------ AI module

    QGCLabel {
        text:       qsTr("AI 추적")
        font.bold:  true
        visible:    root._aiConfigured
    }

    RowLayout {
        Layout.fillWidth:   true
        visible:            root._aiConfigured

        QGCLabel {
            Layout.fillWidth:   true
            text:               root._aiConnected ? qsTr("모듈 연결됨") : qsTr("모듈 연결 안 됨")
            color:              root._aiConnected ? qgcPal.colorGreen : qgcPal.colorOrange
            font.pointSize:     ScreenTools.smallFontPointSize
        }
    }

    RowLayout {
        Layout.fillWidth:   true
        visible:            root._aiConfigured
        enabled:            root._aiConnected

        QGCButton {
            Layout.fillWidth:   true
            text:               SiyiAiController.recognitionEnabled ? qsTr("AI 끄기") : qsTr("AI 켜기")
            onClicked:          SiyiAiController.setRecognition(!SiyiAiController.recognitionEnabled)
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("추적 해제")
            enabled:            SiyiAiController.hasTarget
            onClicked:          SiyiAiController.cancelTracking()
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            root._aiConfigured
        text:               SiyiAiController.hasTarget
                                ? (SiyiAiController.targetLost
                                    ? qsTr("표적 놓침: %1").arg(SiyiAiController.targetTypeName)
                                    : qsTr("추적 중: %1").arg(SiyiAiController.targetTypeName))
                                : qsTr("표적 없음")
        font.pointSize:     ScreenTools.smallFontPointSize
    }
}
