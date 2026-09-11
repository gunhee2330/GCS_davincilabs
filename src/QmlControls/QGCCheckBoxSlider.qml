import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

AbstractButton   {
    id:         control
    checkable:  true
    padding:    0

    // The mockup draws a 26 x 46 track with a 20 knob against its 18px text
    // metric, so the whole thing is kept as a ratio of the font metric
    property real _trackHeight:     Math.round(ScreenTools.defaultFontPixelHeight * 1.45)
    property int  _sliderInset:     Math.round(_trackHeight * 0.115)
    property bool _showHighlight:   enabled && (pressed || checked)

    QGCPalette { id: qgcPal; colorGroupEnabled: control.enabled }

    contentItem: Item {
        implicitWidth:  (label.visible ? label.contentWidth + ScreenTools.defaultFontPixelWidth : 0) + indicator.width
        // The track is now taller than a line of text, so it drives the height
        implicitHeight: Math.max(label.contentHeight, indicator.height)

        QGCLabel {
            id:                     label
            anchors.left:           parent.left
            anchors.verticalCenter: parent.verticalCenter
            text:                   visible ? control.text : "X"
            visible:                control.text !== ""
        }

        Rectangle {
            id:                     indicator
            anchors.right:          parent.right
            anchors.verticalCenter: parent.verticalCenter
            height:                 control._trackHeight
            width:                  Math.round(height * 1.77)
            radius:                 height / 2
            // The OFF track was qgcPal.button, which is the same value as the card it sits on,
            // leaving nothing but the border to find. The border colour is a step up from both,
            // so filling the track with it makes the pill itself visible
            color:                  checked ? qgcPal.buttonHighlight : qgcPal.buttonBorder
            border.width:           1
            border.color:           qgcPal.buttonBorder

            Rectangle {
                anchors.fill:   parent
                color:          qgcPal.buttonHighlight
                opacity:        _showHighlight ? 1 : control.enabled && control.hovered ? .2 : 0
                radius:         parent.radius
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x:                      checked ? indicator.width - width - _sliderInset : _sliderInset
                height:                 parent.height - (_sliderInset * 2)
                width:                  height
                radius:                 height / 2
                // The knob rides on buttonHighlight when checked, so it has to
                // flip to that fill's foreground to stay visible
                color:                  control.checked ? qgcPal.buttonHighlightText : qgcPal.buttonText
            }
        }
    }
}
