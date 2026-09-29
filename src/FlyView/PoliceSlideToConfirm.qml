import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Slide to confirm: a round knob at the left of a rounded track, dragged all the way to the right
/// to emit accepted(). Let go anywhere short of that and it slides back without doing anything.
Rectangle {
    id:             control
    implicitWidth:  ScreenTools.defaultFontPixelHeight * 11
    implicitHeight: Math.max(ScreenTools.minTouchPixels * 1.4, ScreenTools.defaultFontPixelHeight * 2.1)
    radius:         height / 2
    color:          "#1c2733"
    opacity:        enabled ? 1 : 0.4

    property alias text: label.text

    signal accepted()

    readonly property real _inset:    ScreenTools.defaultFontPixelHeight * 0.15
    readonly property real _maxX:     width - knob.width - _inset
    /// How far along the track counts as the end: the last few pixels are hard to land on exactly.
    readonly property real _acceptAt: _inset + (_maxX - _inset) * 0.95

    QGCPalette { id: qgcPal }

    // Fill behind the knob, so the part already slid over reads as done.
    Rectangle {
        x:       control._inset
        y:       control._inset
        width:   knob.x + knob.width - control._inset
        height:  knob.height
        radius:  height / 2
        color:   Qt.darker(qgcPal.buttonHighlight, 1.8)
    }

    QGCLabel {
        id:                  label
        anchors.left:        parent.left
        anchors.leftMargin:  knob.width + control._inset
        anchors.right:       parent.right
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignHCenter
    }

    Rectangle {
        id:         knob
        objectName: "policeSlideKnob"
        x:          control._inset
        y:          control._inset
        height:     control.height - control._inset * 2
        width:      height
        radius:     height / 2
        color:      qgcPal.buttonHighlight

        QGCColoredImage {
            anchors.centerIn: parent
            width:            parent.width * 0.4
            height:           width
            source:           "/InstrumentValueIcons/cheveron-right.svg"
            color:            qgcPal.buttonHighlightText
        }

        MouseArea {
            anchors.fill:    parent
            enabled:         control.enabled
            preventStealing: true
            drag.target:     knob
            drag.axis:       Drag.XAxis
            drag.minimumX:   control._inset
            drag.maximumX:   control._maxX
            drag.threshold:  0

            onReleased: {
                const reached = knob.x >= control._acceptAt
                snapBack.start()
                if (reached) {
                    control.accepted()
                }
            }
            onCanceled: snapBack.start()
        }
    }

    NumberAnimation {
        id:       snapBack
        target:   knob
        property: "x"
        to:       control._inset
        duration: 200
        easing.type: Easing.OutCubic
    }
}
