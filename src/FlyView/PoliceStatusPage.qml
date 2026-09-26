import QtQuick
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// The drawer behind the top bar's status banner.
///
/// One drawer for every state the banner can show, on the ground and in the air alike: the
/// flight log is the same either way, with the takeoff's date and time added on top in the air.
/// The running flight time and distance are deliberately not here - they are on the banner
/// itself, beside the state, which is where an operator reads them without opening anything.
///
/// When arming is refused the reasons come first, because that is what the operator opened the
/// drawer to find.
ToolIndicatorPage {
    id:         page
    objectName: "policeStatusPage"

    /// The banner's own line, so the drawer names the state it was opened from.
    property string headingText: ""

    /// Arming is refused. The reasons are listed above the log when it is.
    property bool armBlocked: false

    /// The arm instant as a Date, from the dashboard; shown while armed.
    property var takeoffTime: null

    showExpand: false

    /// Seconds into HH:mm:ss, or an em dash before this airframe has completed a flight under
    /// this station.
    readonly property string _lastFlightText: {
        const total = App.TakeoffCounter.lastFlightSeconds
        return total < 0 ? "—" : _durationText(total)
    }

    function _durationText(total) {
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        return (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    /// Whole metres, then kilometres to one place from 1 km, as the banner writes the flight in
    /// progress (PoliceDroneDashboard._flightDistanceText).
    function _distanceText(metres) {
        const rounded = Math.round(metres)
        return rounded >= 1000 ? (rounded / 1000).toFixed(1) + " km" : rounded + " m"
    }

    /// PX4 answers the question in machine-readable form. ArduPilot - the delivery airframe -
    /// sends its refusals as STATUSTEXT instead, and nothing in this fork collects those into a
    /// list of their own: they arrive in the vehicle message list, which the pictogram beside
    /// this banner opens. So the sentence below sends the operator there rather than a second
    /// reporting pipeline being built for it here.
    readonly property bool _reasonsAvailable:
        page.activeVehicle && page.activeVehicle.healthAndArmingCheckReport.supported

    readonly property bool _armed:  page.activeVehicle ? page.activeVehicle.armed : false
    readonly property bool _flying: page.activeVehicle ? page.activeVehicle.flying : false

    contentComponent: Component {
        SettingsGroupLayout {
            heading: page.headingText

            LabelledLabel {
                label:     qsTr("이륙 일시")
                labelText: page.takeoffTime ? Qt.formatDateTime(page.takeoffTime, "MM-dd HH:mm:ss") : ""
                visible:   page._armed && page.takeoffTime !== null
            }

            Repeater {
                model: (page.armBlocked && page._reasonsAvailable)
                           ? page.activeVehicle.healthAndArmingCheckReport.problemsForCurrentMode
                           : null

                delegate: QGCLabel {
                    Layout.fillWidth: true
                    wrapMode:         Text.WordWrap
                    textFormat:       TextEdit.RichText
                    text:             object.message
                    color:            object.severity === "error"   ? QGroundControl.globalPalette.colorRed
                                      : object.severity === "warning" ? QGroundControl.globalPalette.colorOrange
                                                                      : QGroundControl.globalPalette.text
                }
            }

            QGCLabel {
                Layout.fillWidth: true
                wrapMode:         Text.WordWrap
                visible:          page.armBlocked && !page._reasonsAvailable
                color:            QGroundControl.globalPalette.colorOrange
                text:             qsTr("시동이 막힌 이유는 기체 메시지에서 확인하십시오")
            }

            // The stock status drawer's hold button (MainStatusIndicator), whose job follows the
            // aircraft. In the air it is the emergency stop, which goes through the guided
            // controller so the confirm control under the bar asks before the motors stop.
            QGCDelayButton {
                objectName:       "policeArmButton"
                Layout.fillWidth: true
                visible:          page.parametersReady
                enabled:          page._armed || !page._reasonsAvailable || page.activeVehicle.healthAndArmingCheckReport.canArm
                text:             page._flying ? qsTr("비상 정지") : (page._armed ? qsTr("시동 끄기") : qsTr("시동"))

                onActivated: {
                    if (page._flying) {
                        mainWindow.disarmVehicleRequest()
                    } else {
                        page.activeVehicle.armed = !page._armed
                    }
                    mainWindow.closeIndicatorDrawer()
                }
            }

            // Stock gates Force Arm behind a switch; here the refusal is the gate, and the guided
            // controller's own Force Arm confirmation asks before the checks are bypassed.
            QGCDelayButton {
                objectName:       "policeForceArmButton"
                Layout.fillWidth: true
                visible:          page.parametersReady && page.armBlocked && !page._armed
                text:             qsTr("강제 시동")

                onActivated: {
                    mainWindow.forceArmVehicleRequest()
                    mainWindow.closeIndicatorDrawer()
                }
            }

            LabelledLabel {
                label:     qsTr("이륙 횟수")
                labelText: qsTr("%1회").arg(App.TakeoffCounter.takeoffCount)
            }

            LabelledLabel {
                label:     qsTr("직전 비행 시간")
                labelText: page._lastFlightText
            }

            LabelledLabel {
                label:     qsTr("지난 비행")
                labelText: App.TakeoffCounter.flights.length === 0 ? qsTr("기록 없음") : ""
            }

            // Newest first, as TakeoffCounter keeps them. Held to a few rows and scrolled inside
            // itself, so a long log leaves the drawer the size it was.
            QGCListView {
                objectName:             "policeFlightList"
                Layout.fillWidth:       true
                Layout.minimumWidth:    ScreenTools.defaultFontPixelWidth * 30
                Layout.preferredHeight: Math.min(contentHeight, ScreenTools.defaultFontPixelHeight * 7)
                visible:                count > 0
                model:                  App.TakeoffCounter.flights

                delegate: RowLayout {
                    required property var modelData

                    width:   ListView.view.width
                    spacing: ScreenTools.defaultFontPixelWidth * 2

                    // takeoff is local ISO 8601, yyyy-MM-ddTHH:mm:ss: cut rather than parsed, so no
                    // time zone gets a say in it.
                    QGCLabel {
                        Layout.fillWidth: true
                        text:             modelData.takeoff.substring(5, 10) + " " + modelData.takeoff.substring(11, 16)
                    }

                    QGCLabel {
                        text: page._durationText(modelData.seconds)
                    }

                    QGCLabel {
                        Layout.minimumWidth: ScreenTools.defaultFontPixelWidth * 7
                        horizontalAlignment: Text.AlignRight
                        text:                page._distanceText(modelData.metres)
                    }
                }
            }
        }
    }
}
