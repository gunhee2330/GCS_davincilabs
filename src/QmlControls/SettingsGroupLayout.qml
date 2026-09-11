import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

ColumnLayout {
    id:                 control
    spacing:            _margins / 2
    implicitWidth:      _contentLayout.implicitWidth + (_margins * 2)
    implicitHeight:     _contentLayout.implicitHeight + (_margins * 2)

    default property alias contentItem: _contentLayout.data

    property alias contentSpacing: _contentLayout.spacing

    property string defaultBorderColor  : QGroundControl.globalPalette.groupBorder
    property string outerBorderColor    : defaultBorderColor

    // Quiet and small, the way the vehicle config sections label their cards: the heading is a
    // signpost, and shouting it competes with the values the operator came to read
    property string defaultHeadingPointSize:    ScreenTools.defaultFontPointSize * 0.85
    property string headingPointSize:           defaultHeadingPointSize

    property string heading
    property string headingDescription
    property bool   showDividers:       true
    property bool   showBorder:         true

    property real _margins: ScreenTools.defaultFontPixelHeight / 2

    // We work with a y sorted list of children for divider visibility checks
    property var _ySortedChildren: {
        let arr = []
        for (let c of _contentLayout.children)
            arr.push(c)
        arr.sort((a, b) => a.y - b.y)
        return arr
    }

    ColumnLayout {
        Layout.leftMargin:  _margins
        Layout.fillWidth:   true
        spacing:            0
        visible:            heading !== ""

        QGCLabel {
            text:           heading
            font.pointSize: headingPointSize
            font.bold:      true
            opacity:        0.6
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               headingDescription
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            visible:            headingDescription !== ""
        }
    }

    Rectangle {
        id:                 outerRect
        Layout.fillWidth:   true
        implicitWidth:      _contentLayout.implicitWidth + (showBorder ? _margins * 2 : 0)
        implicitHeight:     _contentLayout.implicitHeight + (showBorder ? _margins * 2: 0)
        // Same card fill the vehicle config sections use, so both setup screens read as one
        // surface. Borderless callers keep no fill: they have no margins to hold one
        color:              showBorder ? QGroundControl.globalPalette.button : "transparent"
        border.color:       outerBorderColor
        border.width:       showBorder ? 1 : 0
        radius:             ScreenTools.defaultFontPixelHeight / 2

        Repeater {
            model: showDividers ? _ySortedChildren.length : 0

            Rectangle {
                x:          showBorder ? _margins : 0
                y:          _contentItem ? (_contentItem.y + _contentItem.height + _margins + (showBorder ? _margins : 0)) : 0
                width:      parent.width - (showBorder ? _margins * 2 : 0)
                height:     1
                color:      QGroundControl.globalPalette.groupBorder
                visible:    _contentItem ? _isContentItemVisible() : false

                property var _contentItem: index < _ySortedChildren.length ? _ySortedChildren[index] : undefined

                function _isRepeater(item) {
                    return item && item.toString().startsWith("QQuickRepeater");
                }

                function _isContentItemVisible() {
                    if (!_contentItem || !_contentItem.visible || _isRepeater(_contentItem)) {
                        return false
                    }
                    // Any children after this one visually from top to bottom must be visible to show divider
                    for (let i = index + 1; i < _ySortedChildren.length; ++i) {
                        if (!_ySortedChildren[i] || _isRepeater(_ySortedChildren[i])) {
                            continue
                        }
                        if (_ySortedChildren[i].visible) {
                            return true
                        }
                    }
                    return false
                }
            }
        }

        ColumnLayout {
            id:                 _contentLayout
            x:                  showBorder ? _margins : 0
            y:                  showBorder ? _margins : 0
            width:              parent.width - (showBorder ? _margins * 2 : 0)
            spacing:            _margins * (showDividers ? 2 : 1)
        }
    }
}
