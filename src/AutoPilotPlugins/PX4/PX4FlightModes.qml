import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactControls
import QGroundControl.Controls

SetupPage {
    pageComponent:  pageComponent
    Component {
        id: pageComponent

        Item {
            id:     root
            width:  availableWidth
            height: availableHeight

            property string sectionIdFilter: ""

            property var  _switchNameList:  [ "RC_MAP_ARM_SW", "RC_MAP_GEAR_SW", "RC_MAP_KILL_SW", "RC_MAP_LOITER_SW", "RC_MAP_OFFB_SW", "RC_MAP_RETURN_SW" ]
            property var  _switchTHList:    [ "RC_ARMSWITCH_TH", "RC_GEAR_TH", "RC_KILLSWITCH_TH", "RC_LOITER_TH", "RC_OFFB_TH", "RC_RETURN_TH" ]

            readonly property real _channelComboWidth: ScreenTools.defaultFontPixelWidth * 13

            Component.onCompleted: {
                if (controller.vehicle.vtol) {
                    _switchNameList.push("RC_MAP_TRANS_SW")
                    _switchTHList.push("RC_TRANS_TH")
                }
                if (controller.vehicle.fixedWing) {
                    _switchNameList.push("RC_MAP_FLAPS")
                    _switchTHList.push("")
                }
                switchRepeater.model = _switchNameList
            }

            PX4SimpleFlightModesController {
                id: controller
            }

            QGCFlickable {
                anchors.fill:   parent
                clip:           true
                contentWidth:   column1.width
                contentHeight:  column1.height

                // The police settings mockup's captioned cards of rows, one section under the
                // other: a label on the left of each row and its combo box on the right
                ColumnLayout {
                    id:         column1
                    width:      root.width
                    spacing:    ScreenTools.mockupUnit * 1.2

                    SettingsGroupLayout {
                        id:                 flightModeSettings
                        Layout.fillWidth:   true
                        heading:            qsTr("Flight Mode Settings")
                        visible:            sectionIdFilter === "" || sectionIdFilter === "Flight Modes"

                        RowLayout {
                            QGCLabel {
                                Layout.fillWidth:   true
                                text:               qsTr("Mode Channel")
                            }

                            FactComboBox {
                                fact:               controller.getParameterFact(-1, "RC_MAP_FLTMODE")
                                indexModel:         false
                                sizeToContents:     true
                            }
                        }

                        Repeater {
                            model: 6

                            RowLayout {
                                QGCLabel {
                                    Layout.fillWidth:   true
                                    text:               qsTr("Flight Mode %1").arg(modelData + 1)
                                    color:              (controller.activeFlightMode - 1) == index ? "yellow" : qgcPal.text
                                }

                                FactComboBox {
                                    fact:               controller.getParameterFact(-1, "COM_FLTMODE" + (modelData + 1))
                                    indexModel:         false
                                    sizeToContents:     true
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        id:                 column2
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.mockupUnit * 1.2
                        visible:            sectionIdFilter === "" || sectionIdFilter === "Switch Settings"

                        SettingsGroupLayout {
                            Layout.fillWidth:   true
                            heading:            qsTr("Switch Settings")

                            Repeater {
                                id: switchRepeater

                                RowLayout {
                                    spacing:            ScreenTools.defaultFontPixelWidth
                                    Layout.fillWidth:   true

                                    property string thFactName:     _switchTHList[index]
                                    property bool   thFactExists:   thFactName !== ""
                                    property Fact   swFact:         controller.getParameterFact(-1, modelData)
                                    property Fact   thFact:         thFactExists ? controller.getParameterFact(-1, thFactName) : null
                                    property real   thValue:        thFactExists ? thFact.rawValue : 0.5
                                    property real   thPWM:          1000 + (1000 * thValue)
                                    property int    swChannel:      swFact.rawValue - 1
                                    property bool   swActive:       swChannel < 0 ?
                                                                        false :
                                                                        (thValue >= 0 ?
                                                                             (controller.rcChannelValues[swChannel] > thPWM) :
                                                                             (controller.rcChannelValues[swChannel] <= thPWM))
                                    QGCLabel {
                                        text:               swFact.shortDescription
                                        Layout.fillWidth:   true
                                        color:              swActive ? "yellow" : qgcPal.text
                                    }

                                    FactComboBox {
                                        Layout.preferredWidth:  _channelComboWidth
                                        fact:                   swFact
                                        indexModel:             false
                                    }
                                }
                            }
                        }

                        RCChannelMonitor {
                            Layout.fillWidth:   true
                            twoColumn:          true
                        }
                    }
                }
            }
        }
    }
}
