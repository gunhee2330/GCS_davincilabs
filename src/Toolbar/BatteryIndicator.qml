import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

//-------------------------------------------------------------------------
//-- Battery Indicator
Item {
    id:             control
    objectName:     "toolbar_batteryIndicator"
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          batteryIndicatorRow.width

    property bool       showIndicator:      _activeVehicle && _activeVehicle.batteries.count > 0
    property bool       waitForParameters:  false
    property Component  expandedPageComponent

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property var    _batterySettings:   QGroundControl.settingsManager.batteryIndicatorSettings
    property Fact   _indicatorDisplay:  _batterySettings.valueDisplay
    property bool   _showPercentage:    _indicatorDisplay.rawValue === 0
    property bool   _showVoltage:       _indicatorDisplay.rawValue === 1
    property bool   _showBoth:          _indicatorDisplay.rawValue === 2
    property int    _lowestBatteryId:   -1      // -1: show all batteries, otherwise show only battery with this id

    // Properties to hold the thresholds
    property int threshold1: _batterySettings.threshold1.rawValue
    property int threshold2: _batterySettings.threshold2.rawValue

    function _recalcLowestBatteryIdFromVoltage() {
        if (_activeVehicle) {
            // If there is only one battery then it is the lowest
            if (_activeVehicle.batteries.count === 1) {
                _lowestBatteryId = _activeVehicle.batteries.get(0).id.rawValue
                return
            }

            // If we have valid voltage for all batteries we use that to determine lowest battery
            let allHaveVoltage = true
            for (var i = 0; i < _activeVehicle.batteries.count; i++) {
                let battery = _activeVehicle.batteries.get(i)
                if (isNaN(battery.voltage.rawValue)) {
                    allHaveVoltage = false
                    break
                }
            }
            if (allHaveVoltage) {
                let lowestBattery = _activeVehicle.batteries.get(0)
                let lowestBatteryId = lowestBattery.id.rawValue
                for (var i = 1; i < _activeVehicle.batteries.count; i++) {
                    let battery = _activeVehicle.batteries.get(i)
                    if (battery.voltage.rawValue < lowestBattery.voltage.rawValue) {
                        lowestBattery = battery
                        lowestBatteryId = battery.id.rawValue
                    }
                }
                _lowestBatteryId = lowestBatteryId
                return
            }
        }

        // Couldn't determine lowest battery, show all
        _lowestBatteryId = -1
    }

    function _recalcLowestBatteryIdFromPercentage() {
        if (_activeVehicle) {
            // If there is only one battery then it is the lowest
            if (_activeVehicle.batteries.count === 1) {
                _lowestBatteryId = _activeVehicle.batteries.get(0).id.rawValue
                return
            }

            // If we have valid percentage for all batteries we use that to determine lowest battery
            let allHavePercentage = true
            for (var i = 0; i < _activeVehicle.batteries.count; i++) {
                let battery = _activeVehicle.batteries.get(i)
                if (isNaN(battery.percentRemaining.rawValue)) {
                    allHavePercentage = false
                    break
                }
            }
            if (allHavePercentage) {
                let lowestBattery = _activeVehicle.batteries.get(0)
                let lowestBatteryId = lowestBattery.id.rawValue
                for (var i = 1; i < _activeVehicle.batteries.count; i++) {
                    let battery = _activeVehicle.batteries.get(i)
                    if (battery.percentRemaining.rawValue < lowestBattery.percentRemaining.rawValue) {
                        lowestBattery = battery
                        lowestBatteryId = battery.id.rawValue
                    }
                }
                _lowestBatteryId = lowestBatteryId
                return
            }
        }

        // Couldn't determine lowest battery, show all
        _lowestBatteryId = -1
    }

    function _recalcLowestBatteryIdFromChargeState() {
        if (_activeVehicle) {
            // If there is only one battery then it is the lowest
            if (_activeVehicle.batteries.count === 1) {
                _lowestBatteryId = _activeVehicle.batteries.get(0).id.rawValue
                return
            }

            // If we have valid chargeState for all batteries we use that to determine lowest battery
            let allHaveChargeState = true
            for (var i = 0; i < _activeVehicle.batteries.count; i++) {
                let battery = _activeVehicle.batteries.get(i)
                if (battery.chargeState.rawValue === MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_UNDEFINED) {
                    allHaveChargeState = false
                    break
                }
            }
            if (allHaveChargeState) {
                let lowestBattery = _activeVehicle.batteries.get(0)
                let lowestBatteryId = lowestBattery.id.rawValue
                for (var i = 1; i < _activeVehicle.batteries.count; i++) {
                    let battery = _activeVehicle.batteries.get(i)
                    if (battery.chargeState.rawValue > lowestBattery.chargeState.rawValue) {
                        lowestBattery = battery
                        lowestBatteryId = battery.id.rawValue
                    }
                }
                _lowestBatteryId = lowestBatteryId
                return
            }
        }

        // Couldn't determine lowest battery, show all
        _lowestBatteryId = -1
    }

    function _recalcLowestBatteryId() {
        if (!_activeVehicle || _activeVehicle.batteries.count === 0) {
            _lowestBatteryId = -1
            return
        }
        if (_batterySettings.valueDisplay.rawValue === 0) {
            // User wants percentage display so use that if available
            _recalcLowestBatteryIdFromPercentage()
        } else if (_batterySettings.valueDisplay.rawValue === 1) {
            // User wants voltage display so use that if available
            _recalcLowestBatteryIdFromVoltage()
        }
        // If we still dont have a lowest battery id then try charge state
        if (_lowestBatteryId === -1) {
            _recalcLowestBatteryIdFromChargeState()
        }
    }

    Component.onCompleted: _recalcLowestBatteryId()

    Connections {
        target: _activeVehicle ? _activeVehicle.batteries : null
        function onCountChanged() {_recalcLowestBatteryId() }
    }

    QGCPalette { id: qgcPal }

    RowLayout {
        id:             batteryIndicatorRow
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        spacing:        ScreenTools.defaultFontPixelWidth / 2

        Repeater {
            model: _activeVehicle ? _activeVehicle.batteries : 0

            Loader {
                Layout.fillHeight:  true
                sourceComponent:    batteryVisual
                visible:            control._lowestBatteryId === -1 || object.id.rawValue === control._lowestBatteryId || !control._batterySettings.consolidateMultipleBatteries.rawValue

                property var battery: object
            }
        }
    }

    MouseArea {
        anchors.fill:   parent
        onClicked:      mainWindow.showIndicatorDrawer(batteryPopup, control)
    }

    Component {
        id: batteryPopup

        BatteryIndicatorPage { }
    }

    Component {
        id: batteryVisual

        Row {
            Layout.fillHeight:  true
            spacing:            ScreenTools.defaultFontPixelWidth / 4

            function getBatteryColor() {
                switch (battery.chargeState.rawValue) {
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_OK:
                        // A healthy battery stays the same colour as every other reading in the
                        // bar. Colour is reserved for the levels the operator has to act on,
                        // so the one thing that turns yellow is the one thing that matters.
                        if (!isNaN(battery.percentRemaining.rawValue)) {
                            if (battery.percentRemaining.rawValue > threshold2) {
                                return qgcPal.text
                            } else {
                                return qgcPal.colorYellow
                            }
                        } else {
                            return qgcPal.text
                        }
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_LOW:
                        return qgcPal.colorOrange
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_CRITICAL:
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_EMERGENCY:
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_FAILED:
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_UNHEALTHY:
                        return qgcPal.colorRed
                    default:
                        return qgcPal.text
                }
            }

            function getBatterySvgSource() {
                switch (battery.chargeState.rawValue) {
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_OK:
                        if (!isNaN(battery.percentRemaining.rawValue)) {
                            if (battery.percentRemaining.rawValue > threshold1) {
                                return "/qmlimages/BatteryGreen.svg"
                            } else if (battery.percentRemaining.rawValue > threshold2) {
                                return "/qmlimages/BatteryYellowGreen.svg"
                            } else {
                                return "/qmlimages/BatteryYellow.svg"
                            }
                        }
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_LOW:
                        return "/qmlimages/BatteryOrange.svg" // Low with orange svg
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_CRITICAL:
                        return "/qmlimages/BatteryCritical.svg" // Critical with red svg
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_EMERGENCY:
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_FAILED:
                    case MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_UNHEALTHY:
                        return "/qmlimages/BatteryEMERGENCY.svg" // Exclamation mark
                    default:
                        return "/qmlimages/Battery.svg" // Fallback if percentage is unavailable
                }
            }

            function getBatteryPercentageText() {
                if (!isNaN(battery.percentRemaining.rawValue)) {
                    if (battery.percentRemaining.rawValue > 98.9) {
                        return qsTr("100%")
                    } else {
                        return battery.percentRemaining.valueString + battery.percentRemaining.units
                    }
                } else if (!isNaN(battery.voltage.rawValue)) {
                    return battery.voltage.valueString + battery.voltage.units
                } else if (battery.chargeState.rawValue !== MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_UNDEFINED) {
                    return battery.chargeState.enumStringValue
                }
                return qsTr("n/a")
            }

            function getBatteryVoltageText() {
                if (!isNaN(battery.voltage.rawValue)) {
                    return battery.voltage.valueString + battery.voltage.units
                } else if (battery.chargeState.rawValue !== MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_UNDEFINED) {
                    return battery.chargeState.enumStringValue
                }
                return qsTr("n/a")
            }

            Timer {
                id:         debounceRecalcTimer
                interval:   50
                running:    false
                repeat:     false
                onTriggered: {
                    control._recalcLowestBatteryId()
                }
            }
            Connections {
                target: battery.percentRemaining
                function onRawValueChanged() {
                    debounceRecalcTimer.restart()
                }
            }
            Connections {
                target: battery.voltage
                function onRawValueChanged() {
                    debounceRecalcTimer.restart()
                }
            }
            Connections {
                target: battery.chargeState
                function onRawValueChanged() {
                    debounceRecalcTimer.restart()
                }
            }

            QGCColoredImage {
                anchors.top:        parent.top
                anchors.bottom:     parent.bottom
                width:              height
                sourceSize.width:   width
                source:             getBatterySvgSource()
                fillMode:           Image.PreserveAspectFit
                color:              getBatteryColor()
            }

           ColumnLayout {
                id:                     batteryInfoColumn
                anchors.top:            parent.top
                anchors.bottom:         parent.bottom
                spacing:                0

                QGCLabel {
                    Layout.alignment:       Qt.AlignHCenter
                    verticalAlignment:      Text.AlignVCenter
                    color:                  qgcPal.text
                    text:                   getBatteryPercentageText()
                    font.pointSize:         _showBoth ? ScreenTools.defaultFontPointSize : ScreenTools.mediumFontPointSize
                    visible:                _showBoth || _showPercentage
                }

                QGCLabel {
                    Layout.alignment:       Qt.AlignHCenter
                    font.pointSize:         _showBoth ? ScreenTools.defaultFontPointSize : ScreenTools.mediumFontPointSize
                    color:                  qgcPal.text
                    text:                   getBatteryVoltageText()
                    visible:                _showBoth || _showVoltage
                }
            }
        }
    }
}
