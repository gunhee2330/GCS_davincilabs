import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

CheckBox {
    id:             control
    spacing:        _noText ? 0 : ScreenTools.defaultFontPixelWidth
    focusPolicy:    Qt.ClickFocus
    leftPadding:    0

    Component.onCompleted: {
        if (_noText) {
            rightPadding = 0
        }
    }

    property color  textColor:          qgcPal.buttonText
    property bool   textBold:           false
    property real   textFontPointSize:  ScreenTools.defaultFontPointSize
    property ButtonGroup buttonGroup: null

    property bool _noText: text === ""

    QGCPalette { id: qgcPal; colorGroupEnabled: control.enabled }

    onButtonGroupChanged: {
        if (buttonGroup) {
            buttonGroup.addButton(control)
        }
    }

    contentItem: Text {
        //implicitWidth:  _noText ? 0 : text.implicitWidth + ScreenTools.defaultFontPixelWidth * 0.25
        //implicitHeight: _noText ? 0 : Math.max(text.implicitHeight, ScreenTools.checkBoxIndicatorSize)
        leftPadding:        control.indicator.width + control.spacing
        verticalAlignment:  Text.AlignVCenter
        text:               control.text
        font.pointSize:     textFontPointSize
        font.bold:          control.textBold
        font.family:        ScreenTools.normalFontFamily
        color:              control.textColor
    }

    indicator:  Rectangle {
        implicitWidth:  ScreenTools.implicitCheckBoxHeight
        implicitHeight: implicitWidth
        x:              control.leftPadding
        y:              parent.height / 2 - height / 2
        // Checked is a filled accent box with a dark tick, so the box takes the
        // highlight fill and the glyph below takes that fill's foreground
        color:          control.checked ? qgcPal.buttonHighlight : qgcPal.textField
        border.color:   control.checked ? qgcPal.buttonHighlight : qgcPal.buttonBorder
        // Mockup box is 18px with a 2px border and a 4px corner
        border.width:   Math.max(1, Math.round(width / 9))
        radius:         Math.round(width * 0.22)
        opacity:        control.checkedState === Qt.PartiallyChecked ? 0.5 : 1

        Rectangle {
            anchors.fill:   parent
            color:          qgcPal.buttonHighlight
            opacity:        control.hovered ? .2 : 0
            radius:         parent.radius
        }

        QGCColoredImage {
            source:             "/qmlimages/checkbox-check.svg"
            color:              qgcPal.buttonHighlightText
            mipmap:             true
            fillMode:           Image.PreserveAspectFit
            width:              parent.implicitWidth * 0.75
            height:             width
            sourceSize.height:  height
            visible:            control.checked
            anchors.centerIn:   parent
        }
    }
}
