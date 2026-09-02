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
            text:               root._connected ? qsTr("Connected") : qsTr("Not connected")
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
        text:               root._connected ? qsTr("Reconnect") : qsTr("Connect")
        onClicked:          SiyiCameraController.start()
    }

    // ------------------------------------------------------------------ Gimbal

    QGCLabel {
        text:       qsTr("Gimbal")
        font.bold:  true
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               qsTr("Yaw %1°   Pitch %2°")
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
            text:                   qsTr("Center")
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

        QGCLabel { text: qsTr("Mode") }

        QGCComboBox {
            Layout.fillWidth:   true
            model:              [qsTr("Lock"), qsTr("Follow"), qsTr("FPV")]
            currentIndex:       SiyiCameraController.motionMode
            onActivated:        (index) => SiyiCameraController.setMotionMode(index)
        }
    }

    // ------------------------------------------------------------------ Camera

    QGCLabel {
        text:       qsTr("Camera")
        font.bold:  true
    }

    RowLayout {
        Layout.fillWidth:   true
        enabled:            root._connected

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Zoom −")
            onPressed:          SiyiCameraController.zoom(-1)
            onReleased:         SiyiCameraController.zoom(0)
            onCanceled:         SiyiCameraController.zoom(0)
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Zoom +")
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
        text:               qsTr("Zoom %1x").arg(SiyiCameraController.zoomMultiple.toFixed(1))
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    RowLayout {
        Layout.fillWidth:   true
        enabled:            root._connected

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Photo")
            onClicked:          SiyiCameraController.takePhoto()
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               SiyiCameraController.recording ? qsTr("Stop") : qsTr("Record")
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
        text:       qsTr("Sensors")
        font.bold:  true
        visible:    root._isZT30
    }

    QGCComboBox {
        Layout.fillWidth:   true
        visible:            root._isZT30
        enabled:            root._connected
        // Index maps directly to the SDK's camera image type values.
        model: [
            qsTr("Zoom + thermal PIP / wide sub"),
            qsTr("Wide + thermal PIP / zoom sub"),
            qsTr("Zoom + wide PIP / thermal sub"),
            qsTr("Zoom main / thermal sub"),
            qsTr("Zoom main / wide sub"),
            qsTr("Wide main / thermal sub"),
            qsTr("Wide main / zoom sub"),
            qsTr("Thermal main / zoom sub"),
            qsTr("Thermal main / wide sub")
        ]
        onActivated: (index) => SiyiCameraController.setCameraImageType(index)
    }

    RowLayout {
        Layout.fillWidth:   true
        visible:            root._isZT30
        enabled:            root._connected

        QGCLabel { text: qsTr("Palette") }

        QGCComboBox {
            Layout.fillWidth:   true
            // Value 1 is unused in the SIYI palette table, hence the explicit value list.
            property var paletteValues: [0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
            model: [
                qsTr("White hot"), qsTr("Sepia"), qsTr("Iron bow"), qsTr("Rainbow"),
                qsTr("Night"), qsTr("Aurora"), qsTr("Red hot"), qsTr("Jungle"),
                qsTr("Medical"), qsTr("Black hot"), qsTr("Glory hot")
            ]
            onActivated: (index) => SiyiCameraController.setThermalPalette(paletteValues[index])
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            root._isZT30
        text:               SiyiCameraController.thermalRangeAvailable
                                ? qsTr("Thermal %1 °C … %2 °C")
                                    .arg(SiyiCameraController.thermalMinTempC.toFixed(1))
                                    .arg(SiyiCameraController.thermalMaxTempC.toFixed(1))
                                : qsTr("Thermal data unavailable")
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            root._isZT30
        text:               SiyiCameraController.rangefinderAvailable
                                ? qsTr("Rangefinder %1 m").arg(SiyiCameraController.rangefinderDistance.toFixed(0))
                                : qsTr("Rangefinder unavailable")
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    // ------------------------------------------------------------------ AI module

    QGCLabel {
        text:       qsTr("AI Tracking")
        font.bold:  true
        visible:    root._aiConfigured
    }

    RowLayout {
        Layout.fillWidth:   true
        visible:            root._aiConfigured

        QGCLabel {
            Layout.fillWidth:   true
            text:               root._aiConnected ? qsTr("Module connected") : qsTr("Module not connected")
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
            text:               SiyiAiController.recognitionEnabled ? qsTr("AI On") : qsTr("AI Off")
            onClicked:          SiyiAiController.setRecognition(!SiyiAiController.recognitionEnabled)
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Cancel Track")
            enabled:            SiyiAiController.hasTarget
            onClicked:          SiyiAiController.cancelTracking()
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            root._aiConfigured
        text:               SiyiAiController.hasTarget
                                ? (SiyiAiController.targetLost
                                    ? qsTr("Target lost: %1").arg(SiyiAiController.targetTypeName)
                                    : qsTr("Tracking: %1").arg(SiyiAiController.targetTypeName))
                                : qsTr("No target")
        font.pointSize:     ScreenTools.smallFontPointSize
    }
}
