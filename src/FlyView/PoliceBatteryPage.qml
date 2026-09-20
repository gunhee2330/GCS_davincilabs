import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// The drawer behind the top bar's battery group. The police bar showed a percentage and opened
/// nothing; these are the rows stock QGC already reads off the same fact group
/// (BatteryIndicator.qml), in Korean and for the one pack the bar is showing - the lowest.
///
/// A fact that has never been reported reads NaN, and a row printing NaN is worse than a row
/// that is not there, so each one is hidden on its own availability rather than on the pack's.
ToolIndicatorPage {
    id:         page
    objectName: "policeBatteryPage"

    /// The pack the bar shows: PoliceDroneDashboard._lowestBattery.
    property var battery: null

    showExpand: false

    contentComponent: Component {
        SettingsGroupLayout {
            heading: qsTr("배터리")

            LabelledLabel {
                label:     qsTr("충전 상태")
                labelText: page.battery ? page.battery.chargeState.enumStringValue : ""
                visible:   page.battery &&
                           (page.battery.chargeState.rawValue !== MAVLinkEnums.MAV_BATTERY_CHARGE_STATE_UNDEFINED)
            }

            LabelledLabel {
                label:     qsTr("남은 시간")
                labelText: page.battery ? page.battery.timeRemainingStr.value : ""
                visible:   page.battery && !isNaN(page.battery.timeRemaining.rawValue)
            }

            LabelledLabel {
                label:     qsTr("잔량")
                labelText: page.battery
                               ? page.battery.percentRemaining.valueString + " " + page.battery.percentRemaining.units
                               : ""
                visible:   page.battery && !isNaN(page.battery.percentRemaining.rawValue)
            }

            LabelledLabel {
                label:     qsTr("전압")
                labelText: page.battery
                               ? page.battery.voltage.valueString + " " + page.battery.voltage.units : ""
                visible:   page.battery && !isNaN(page.battery.voltage.rawValue)
            }

            LabelledLabel {
                label:     qsTr("사용량")
                labelText: page.battery
                               ? page.battery.mahConsumed.valueString + " " + page.battery.mahConsumed.units : ""
                visible:   page.battery && !isNaN(page.battery.mahConsumed.rawValue)
            }

            LabelledLabel {
                label:     qsTr("온도")
                labelText: page.battery
                               ? page.battery.temperature.valueString + " " + page.battery.temperature.units : ""
                visible:   page.battery && !isNaN(page.battery.temperature.rawValue)
            }
        }
    }
}
