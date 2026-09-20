import QtQuick
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// The drawer behind the top bar's status banner.
///
/// One drawer for every state the banner can show, on the ground and in the air alike: the
/// flight log is the same two lines either way. The running flight time is deliberately not
/// here - it is on the banner itself, beside the state, which is where an operator reads it
/// without opening anything.
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

    showExpand: false

    /// Seconds into HH:mm:ss, or an em dash before this airframe has completed a flight under
    /// this station.
    readonly property string _lastFlightText: {
        const total = App.TakeoffCounter.lastFlightSeconds
        if (total < 0) {
            return "—"
        }
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        return (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    /// PX4 answers the question in machine-readable form. ArduPilot - the delivery airframe -
    /// sends its refusals as STATUSTEXT instead, and nothing in this fork collects those into a
    /// list of their own: they arrive in the vehicle message list, which the pictogram beside
    /// this banner opens. So the sentence below sends the operator there rather than a second
    /// reporting pipeline being built for it here.
    readonly property bool _reasonsAvailable:
        page.activeVehicle && page.activeVehicle.healthAndArmingCheckReport.supported

    contentComponent: Component {
        SettingsGroupLayout {
            heading: page.headingText

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

            LabelledLabel {
                label:     qsTr("이륙 횟수")
                labelText: qsTr("%1회").arg(App.TakeoffCounter.takeoffCount)
            }

            LabelledLabel {
                label:     qsTr("직전 비행 시간")
                labelText: page._lastFlightText
            }
        }
    }
}
