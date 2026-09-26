import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

ColumnLayout {
    id: root
    Layout.fillWidth: true
    spacing: 0

    property string labelText: "Label"
    property string valueText: "value"
    property string valueColor: ""
    property bool showDivider: true

    RowLayout {
        id: rowLayout
        Layout.fillWidth: true
        Layout.topMargin: ScreenTools.defaultFontPixelHeight * 0.2
        Layout.bottomMargin: ScreenTools.defaultFontPixelHeight * 0.2
        spacing: ScreenTools.defaultFontPixelHeight

        // The police mockup's key-value rows: 1.15 cqw, the key dimmed
        QGCLabel {
            id: label
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
            text: root.labelText
            wrapMode: Text.WordWrap
            font.pointSize: ScreenTools.mockupPointUnit * 1.15
            color: QGroundControl.globalPalette.secondaryText
        }

        QGCLabel {
            id: valueLabel
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            horizontalAlignment: Text.AlignRight
            text: root.valueText
            color: root.valueColor !== "" ? root.valueColor : QGroundControl.globalPalette.text
            wrapMode: Text.WordWrap
            font.pointSize: ScreenTools.mockupPointUnit * 1.15
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        // One device pixel: the software renderer rounds a thinner fill up to a whole logical
        // pixel, so a whole one is scaled down, and kept just short of opaque so the renderer
        // still draws what lies under the rest of its bounds
        transform: Scale { yScale: ScreenTools.hairline }
        opacity: 0.999
        color: QGroundControl.globalPalette.cardBorder
        visible: root.showDivider
    }
}
