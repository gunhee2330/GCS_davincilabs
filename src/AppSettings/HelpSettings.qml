import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Rectangle {
    objectName: "settingsPage_Help"
    color:          qgcPal.settingsPanel
    anchors.fill:   parent

    readonly property real _margins: ScreenTools.defaultFontPixelHeight

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCFlickable {
        id:                 flickable
        anchors.margins:    _margins
        anchors.fill:       parent
        contentWidth:       grid.width
        contentHeight:      grid.height
        clip:               true

        // A settings card of rows after the police mockup: each name on the left, its link on
        // the right
        SettingsGroupLayout {
            id:         grid
            width:      flickable.width

            RowLayout {
                QGCLabel { text: qsTr("QGroundControl User Guide") }
                QGCLabel {
                    Layout.fillWidth:   true
                    horizontalAlignment: Text.AlignRight
                    wrapMode:           Text.WrapAnywhere
                    linkColor:          qgcPal.text
                    text:               "<a href=\"https://docs.qgroundcontrol.com\">https://docs.qgroundcontrol.com</a>"
                    onLinkActivated:    (link) => Qt.openUrlExternally(link)
                }
            }

            RowLayout {
                QGCLabel { text: qsTr("PX4 Users Discussion Forum") }
                QGCLabel {
                    Layout.fillWidth:   true
                    horizontalAlignment: Text.AlignRight
                    wrapMode:           Text.WrapAnywhere
                    linkColor:          qgcPal.text
                    text:               "<a href=\"http://discuss.px4.io/c/qgroundcontrol\">http://discuss.px4.io/c/qgroundcontrol</a>"
                    onLinkActivated:    (link) => Qt.openUrlExternally(link)
                }
            }

            RowLayout {
                QGCLabel { text: qsTr("ArduPilot Users Discussion Forum") }
                QGCLabel {
                    Layout.fillWidth:   true
                    horizontalAlignment: Text.AlignRight
                    wrapMode:           Text.WrapAnywhere
                    linkColor:          qgcPal.text
                    text:               "<a href=\"https://discuss.ardupilot.org/c/ground-control-software/qgroundcontrol\">https://discuss.ardupilot.org/c/ground-control-software/qgroundcontrol</a>"
                    onLinkActivated:    (link) => Qt.openUrlExternally(link)
                }
            }

            RowLayout {
                QGCLabel { text: qsTr("QGroundControl Discord Channel") }
                QGCLabel {
                    Layout.fillWidth:   true
                    horizontalAlignment: Text.AlignRight
                    wrapMode:           Text.WrapAnywhere
                    linkColor:          qgcPal.text
                    text:               "<a href=\"https://discord.com/channels/1022170275984457759/1022185820683255908\">https://discord.com/channels/1022170275984457759/1022185820683255908</a>"
                    onLinkActivated:    (link) => Qt.openUrlExternally(link)
                }
            }
        }
    }
}
