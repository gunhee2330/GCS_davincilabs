import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

Slider {
    property bool zeroCentered: false ///< Value indicator starts display from zero instead of min value
    property bool displayValue: false ///< true: Show value on handle
    property bool showBoundaryValues: false ///< true: Show min/max values at slider ends

    id: control
    implicitHeight: ScreenTools.implicitSliderHeight + (showBoundaryValues ? minLabel.contentHeight : 0)
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    wheelEnabled: false

    property real _implicitBarLength: Math.round(ScreenTools.defaultFontPixelWidth * 20)
    // Under the settings views, the police mockup's groove and knob, .35 and 1.3 cqw. Drawing
    // only: the control's own height, and so its touch band, is implicitSliderHeight either way
    property real _barHeight: _settingsLook ? ScreenTools.mockupUnit * 0.35 : Math.round(ScreenTools.defaultFontPixelHeight / 3)
    readonly property bool _settingsLook: ScreenTools.inSettingsLook(control)

    QGCPalette { id: qgcPal; colorGroupEnabled: control.enabled }

    background: Rectangle {
        x: control.horizontal ? control.leftPadding : control.leftPadding + control.availableWidth / 2 - width / 2
        y: control.horizontal ? control.topPadding + control.availableHeight / 2 - height / 2 : control.topPadding
        implicitWidth: control.horizontal ? control._implicitBarLength : control._barHeight
        implicitHeight: control.horizontal ? control._barHeight : control._implicitBarLength
        width: control.horizontal ? control.availableWidth : implicitWidth
        height: control.horizontal ? implicitHeight : control.availableHeight
        radius: control._barHeight / 2
        color: control._settingsLook ? qgcPal.controlTrack : qgcPal.button
        border.width: control._settingsLook ? 0 : 1
        border.color: qgcPal.buttonText

        // The settings mockup's accent up to the knob
        Rectangle {
            y:      control.horizontal ? 0 : parent.height * control.visualPosition
            width:  control.horizontal ? parent.width * control.visualPosition : parent.width
            height: control.horizontal ? parent.height : parent.height * (1 - control.visualPosition)
            radius: parent.radius
            color:  qgcPal.buttonHighlight
            visible: control._settingsLook
        }
    }

    handle: Rectangle {
        x: control.horizontal ?
               control.leftPadding + control.visualPosition * (control.availableWidth - width) :
               control.leftPadding + control.availableWidth / 2 - width / 2
        y: control.horizontal ?
               control.topPadding + control.availableHeight / 2 - height / 2 :
               control.topPadding + control.visualPosition * (control.availableHeight - height)
        implicitWidth: _radius * 2
        implicitHeight: _radius * 2
        // The settings mockup's plain white knob. The light card is white too, so there a
        // groove-coloured edge keeps the knob findable
        color: control._settingsLook ? "white" : qgcPal.button
        border.color: control._settingsLook ? qgcPal.controlTrack : qgcPal.buttonText
        border.width: !control._settingsLook || qgcPal.globalTheme === QGCPalette.Light ? 1 : 0
        radius: _radius

        // Whole pixels, or the software renderer floors the corner and squares the knob off
        property real _radius: control._settingsLook && !control.displayValue ? Math.round(ScreenTools.mockupUnit * 0.65) : ScreenTools.defaultFontPixelHeight / 2

        Label {
            text: control.value.toFixed(control.to <= 1 ? 1 : 0)
            visible: control.displayValue
            anchors.centerIn: parent
            font.family: ScreenTools.normalFontFamily
            font.pointSize: ScreenTools.smallFontPointSize
            color: control._settingsLook ? "black" : qgcPal.buttonText
        }
    }

    QGCLabel {
        id: minLabel
        anchors.left: parent.left
        anchors.leftMargin: control.leftPadding
        anchors.bottom: parent.bottom
        text: control.from.toFixed(1)
        font.pointSize: ScreenTools.smallFontPointSize
        color: qgcPal.buttonText
        visible: control.showBoundaryValues
    }

    QGCLabel {
        id: maxLabel
        anchors.right: parent.right
        anchors.rightMargin: control.rightPadding
        anchors.bottom: parent.bottom
        text: control.to.toFixed(1)
        font.pointSize: ScreenTools.smallFontPointSize
        color: qgcPal.buttonText
        visible: control.showBoundaryValues
    }
}
