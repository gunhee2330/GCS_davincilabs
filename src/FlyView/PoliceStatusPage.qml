import QtQuick
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// The drawer behind the top bar's status banner.
///
/// One drawer for every state the banner can show, on the ground and in the air alike: the
/// flight log is the same either way, with the takeoff's date and time added above it in the air.
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
    /// The distance the banner sets apart from that line in the air; empty otherwise.
    property string headingDistance: ""

    /// Arming is refused. The reasons are listed above the log when it is.
    property bool armBlocked: false

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

    /// PX4, the delivery airframe, answers the question in machine-readable form. ArduPilot
    /// sends its refusals as STATUSTEXT instead, and nothing in this fork collects those into a
    /// list of their own: they arrive in the vehicle message list, which the pictogram beside
    /// this banner opens. So where the report is not supported the sentence below sends the
    /// operator there rather than a second reporting pipeline being built for it here.
    readonly property bool _reasonsAvailable:
        page.activeVehicle && page.activeVehicle.healthAndArmingCheckReport.supported

    readonly property bool _armed:  page.activeVehicle ? page.activeVehicle.armed : false
    readonly property bool _flying: page.activeVehicle ? page.activeVehicle.flying : false

    contentComponent: Component {
        // SettingsGroupLayout's heading, drawn here in its place and its look, so the distance in
        // the air can stand apart from the time as it does on the banner.
        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelHeight / 4

            Row {
                Layout.leftMargin: ScreenTools.defaultFontPixelHeight / 2
                spacing:           ScreenTools.defaultFontPixelWidth * 2
                visible:           page.headingText !== ""

                QGCLabel {
                    text:           page.headingText
                    font.pointSize: ScreenTools.defaultFontPointSize * 0.85
                    font.bold:      true
                    opacity:        0.6
                }

                QGCLabel {
                    text:           page.headingDistance
                    font.pointSize: ScreenTools.defaultFontPointSize * 0.85
                    font.bold:      true
                    opacity:        0.6
                    visible:        text !== ""
                }
            }

            SettingsGroupLayout {
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

                // The cycle's first liftoff, the string its flight record keeps: local ISO 8601, cut
                // rather than parsed like the list below. Empty while armed on the pad.
                LabelledLabel {
                    label:     qsTr("이륙 일시")
                    labelText: App.TakeoffCounter.takeoffTime.replace("T", " ")
                    visible:   page._armed && App.TakeoffCounter.takeoffTime !== ""
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
                    Layout.minimumWidth:    ScreenTools.defaultFontPixelWidth * 40
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
                            text:             modelData.takeoff.substring(0, 10) + " " + modelData.takeoff.substring(11, 16)
                        }

                        QGCLabel {
                            text: page._durationText(modelData.seconds)
                        }

                        // In the app's distance unit to one place, as the banner writes the flight in progress,
                        // in a column as wide as a five-digit figure so the durations line up.
                        QGCLabel {
                            Layout.minimumWidth: ScreenTools.defaultFontPixelWidth * 10
                            horizontalAlignment: Text.AlignRight
                            text:                QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnitsString(modelData.metres)
                        }
                    }
                }

                // Every flight of every airframe, on a page of its own for any span of days and the
                // export the spec asks for. There whatever this list holds: another airframe's
                // flights may be all there is.
                Item {
                    objectName:         "policeFlightRecordsLink"
                    Layout.fillWidth:   true
                    implicitHeight:     recordsLinkLabel.implicitHeight

                    QGCLabel {
                        id:                     recordsLinkLabel
                        anchors.left:           parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text:                   qsTr("전체 기록")
                        color:                  QGroundControl.globalPalette.buttonHighlight
                        font.weight:            Font.DemiBold
                    }

                    QGCColoredImage {
                        anchors.right:          parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width:                  ScreenTools.defaultFontPixelHeight * 0.7
                        height:                 width
                        source:                 "/InstrumentValueIcons/cheveron-right.svg"
                        color:                  QGroundControl.globalPalette.secondaryText
                    }

                    QGCMouseArea {
                        fillItem: parent
                        onClicked: {
                            if (mainWindow.allowViewSwitch()) {
                                mainWindow.closeIndicatorDrawer()
                                mainWindow.showTool(qsTr("비행 기록"), "qrc:/qml/QGroundControl/FlyView/PoliceFlightRecordsPage.qml", "")
                            }
                        }
                    }
                }
            }
        }
    }
}
