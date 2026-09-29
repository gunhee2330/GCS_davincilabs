import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

/// A horizontal altitude slider: the value in a bubble above the knob, the minimum under the left
/// end, and at the right end a field labelled 최대 that sets the slider's maximum. Values are in the
/// app's vertical distance unit. The field does not write the maximum itself: it reports the typed
/// number through maximumEdited and whoever owns the maximum stores it.
Item {
    id:             control
    implicitWidth:  ScreenTools.defaultFontPixelHeight * 11
    implicitHeight: bubble.height + bubbleTip.height + slider.height + minimumLabel.height

    property alias value:        slider.value
    property real  minimum:      0
    property real  maximum:      100
    /// The highest number the 최대 field accepts.
    property real  maximumLimit: 1000

    signal maximumEdited(real newMaximum)

    readonly property string _unit: QGroundControl.unitsConversion.appSettingsVerticalDistanceUnitsString

    QGCPalette { id: qgcPal }

    Slider {
        id:           slider
        objectName:   "policeAltitudeSlider"
        anchors.left:  parent.left
        anchors.right: maxField.left
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 2
        y:            bubble.height + bubbleTip.height
        height:       ScreenTools.defaultFontPixelHeight * 1.4
        from:         control.minimum
        to:           control.maximum
        stepSize:     1
        snapMode:     Slider.SnapAlways
        leftPadding:  handle.width / 2
        rightPadding: handle.width / 2

        background: Rectangle {
            x:      slider.leftPadding
            y:      (slider.height - height) / 2
            width:  slider.availableWidth
            height: ScreenTools.defaultFontPixelHeight * 0.2
            radius: height / 2
            color:  "#2b3a4a"

            Rectangle {
                width:  slider.visualPosition * parent.width
                height: parent.height
                radius: height / 2
                color:  qgcPal.buttonHighlight
            }
        }

        handle: Rectangle {
            x:            slider.leftPadding + slider.visualPosition * slider.availableWidth - width / 2
            y:            (slider.height - height) / 2
            width:        ScreenTools.defaultFontPixelHeight * 0.65
            height:       width
            radius:       width / 2
            color:        "white"
            border.color: qgcPal.buttonHighlight
            border.width: 2
        }
    }

    Rectangle {
        id:         bubble
        objectName: "policeAltitudeBubble"
        x:          Math.max(0, Math.min(control.width - width,
                                         slider.x + slider.handle.x + slider.handle.width / 2 - width / 2))
        width:      bubbleLabel.contentWidth + ScreenTools.defaultFontPixelWidth * 2
        height:     bubbleLabel.contentHeight + ScreenTools.defaultFontPixelHeight * 0.3
        radius:     ScreenTools.defaultFontPixelHeight * 0.15
        color:      qgcPal.buttonHighlight

        QGCLabel {
            id:               bubbleLabel
            objectName:       "policeAltitudeBubbleText"
            anchors.centerIn: parent
            color:            qgcPal.buttonHighlightText
            text:             qsTr("%1 %2").arg(Math.round(slider.value)).arg(control._unit)
        }
    }

    // The bubble's point, down at the knob.
    Rectangle {
        id:       bubbleTip
        x:        slider.x + slider.handle.x + slider.handle.width / 2 - width / 2
        y:        bubble.height - height / 2
        width:    ScreenTools.defaultFontPixelHeight * 0.3
        height:   width
        rotation: 45
        color:    qgcPal.buttonHighlight
    }

    QGCLabel {
        id:             minimumLabel
        anchors.top:    slider.bottom
        x:              slider.x + slider.leftPadding - width / 2
        text:           Math.round(control.minimum)
        color:          qgcPal.colorGrey
        font.pointSize: ScreenTools.smallFontPointSize
    }

    QGCLabel {
        anchors.bottom:           maxField.top
        anchors.horizontalCenter: maxField.horizontalCenter
        text:                     qsTr("최대")
        color:                    qgcPal.colorGrey
        font.pointSize:           ScreenTools.smallFontPointSize
    }

    QGCTextField {
        id:                     maxField
        objectName:             "policeAltitudeMaximum"
        anchors.right:          unitLabel.left
        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth / 2
        anchors.verticalCenter: slider.verticalCenter
        width:                  ScreenTools.defaultFontPixelWidth * 9
        numericValuesOnly:      true
        text:                   Math.round(control.maximum)
        validator:              IntValidator { bottom: Math.floor(control.minimum) + 1; top: control.maximumLimit }

        onEditingFinished: {
            if (acceptableInput) {
                control.maximumEdited(Number(text))
            }
            // Back to showing the stored maximum, whether or not the typed number was taken.
            text = Qt.binding(function() { return Math.round(control.maximum) })
        }
    }

    QGCLabel {
        id:                     unitLabel
        anchors.right:          parent.right
        anchors.verticalCenter: maxField.verticalCenter
        text:                   control._unit
        color:                  qgcPal.colorGrey
    }
}
