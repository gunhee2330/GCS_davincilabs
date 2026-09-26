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
    // metric, so the whole thing is kept as a ratio of the font metric. Under the settings views
    // it is the police settings mockup's pill: 1.8 cqw, in whole pixels so the knob stays round
    property real _trackHeight:     _settingsLook ? Math.round(ScreenTools.mockupUnit * 1.8) : Math.round(ScreenTools.defaultFontPixelHeight * 1.45)
    property int  _sliderInset:     Math.round(_trackHeight * 0.115)
    readonly property bool _settingsLook: ScreenTools.inSettingsLook(control)
    // A finger's tap target around the drawn control
    containmentMask: _settingsLook ? _touchArea : null
    SettingsTouchArea { id: _touchArea; visible: control._settingsLook }
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
            // so filling the track with it makes the pill itself visible. The settings mockup's
            // pill has a groove colour of its own and no edge
            color:                  checked ? qgcPal.buttonHighlight : (control._settingsLook ? qgcPal.controlTrack : qgcPal.buttonBorder)
            border.width:           control._settingsLook ? 0 : 1
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
                // flip to that fill's foreground to stay visible. White on both in the settings mockup
                color:                  control._settingsLook ? "white" : (control.checked ? qgcPal.buttonHighlightText : qgcPal.buttonText)
            }
        }
    }
}
