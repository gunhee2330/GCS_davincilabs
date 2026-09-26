import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

ColumnLayout {
    id:                 control
    // Caption to card, .45 cqw in the police mockup
    spacing:            settingsLook ? ScreenTools.mockupUnit * 0.45 : _margins / 2
    implicitWidth:      _contentLayout.implicitWidth + (_margins * 2)
    implicitHeight:     _contentLayout.implicitHeight + (_verticalMargins * 2)

    default property alias contentItem: _contentLayout.data

    property alias contentSpacing: _contentLayout.spacing

    /// The settings screens' card after the police mockup: a hairline edge, a quiet caption, rows
    /// ruled edge to edge and a stock Labelled* row set in the generated rows' type and control
    /// sizes. On only under a view that asks for it (settingsMockupLook on the settings and vehicle
    /// setup roots), so a fly view drawer, a drop panel or a toolbar popup keeps its stock card
    property bool   settingsLook:       ScreenTools.inSettingsLook(parent)

    property string defaultBorderColor  : settingsLook ? QGroundControl.globalPalette.cardBorder : QGroundControl.globalPalette.groupBorder
    property string outerBorderColor    : defaultBorderColor

    // Quiet and small, the way the vehicle config sections label their cards: the heading is a
    // signpost, and shouting it competes with the values the operator came to read
    property string defaultHeadingPointSize:    settingsLook ? ScreenTools.mockupPointUnit * 0.95 : ScreenTools.defaultFontPointSize * 0.85
    property string headingPointSize:           defaultHeadingPointSize

    property string heading
    property string headingDescription
    property bool   showDividers:       true
    property bool   showBorder:         true

    // A mockup row is padded 1 cqw down and 1.4 cqw across, and rows sit one padding pair apart
    // with the rule halfway, so the same two numbers pad the card and space its children
    property real _margins:         settingsLook ? ScreenTools.mockupUnit * 1.4 : ScreenTools.defaultFontPixelHeight / 2
    property real _verticalMargins: settingsLook ? ScreenTools.mockupUnit : _margins

    // We work with a y sorted list of children for divider visibility checks
    property var _ySortedChildren: {
        let arr = []
        for (let c of _contentLayout.children)
            arr.push(c)
        arr.sort((a, b) => a.y - b.y)
        return arr
    }

    ColumnLayout {
        Layout.leftMargin:  settingsLook ? 0 : _margins
        Layout.fillWidth:   true
        spacing:            0
        visible:            heading !== ""

        QGCLabel {
            text:               heading
            font.pointSize:     headingPointSize
            font.bold:          !settingsLook
            font.letterSpacing: settingsLook ? ScreenTools.mockupUnit * 0.019 : 0    // .02em of the caption
            color:              settingsLook ? QGroundControl.globalPalette.secondaryText : QGroundControl.globalPalette.text
            opacity:            settingsLook ? 1 : 0.6
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               headingDescription
            wrapMode:           Text.WordWrap
            font.pointSize:     settingsLook ? ScreenTools.mockupPointUnit * 0.9 : ScreenTools.smallFontPointSize
            color:              settingsLook ? QGroundControl.globalPalette.secondaryText : QGroundControl.globalPalette.text
            visible:            headingDescription !== ""
        }
    }

    Rectangle {
        id:                 outerRect
        Layout.fillWidth:   true
        implicitWidth:      _contentLayout.implicitWidth + (showBorder ? _margins * 2 : 0)
        implicitHeight:     _contentLayout.implicitHeight + (showBorder ? _verticalMargins * 2: 0)
        // Same card fill the vehicle config sections use, so both setup screens read as one
        // surface. Borderless callers keep no fill: they have no margins to hold one
        color:              showBorder ? (settingsLook ? QGroundControl.globalPalette.card : QGroundControl.globalPalette.button) : "transparent"
        border.color:       outerBorderColor
        border.width:       showBorder ? (settingsLook ? ScreenTools.hairline : 1) : 0
        // A whole-pixel snap would round a one device pixel line away
        border.pixelAligned: !settingsLook
        radius:             settingsLook ? ScreenTools.mockupUnit * 0.6 : ScreenTools.defaultFontPixelHeight / 2

        Repeater {
            model: showDividers ? _ySortedChildren.length : 0

            // The settings look rules its rows edge to edge inside the card border
            Rectangle {
                x:          showBorder ? (settingsLook ? ScreenTools.hairline : _margins) : 0
                y:          _contentItem ? (_contentItem.y + _contentItem.height + _verticalMargins + (showBorder ? _verticalMargins : 0)) : 0
                width:      parent.width - (showBorder ? (settingsLook ? ScreenTools.hairline * 2 : _margins * 2) : 0)
                height:     1
                // A fill this thin is rounded up to a whole logical pixel by the software renderer;
                // one scaled down from a whole pixel lands on a single device pixel
                transform:  Scale { yScale: control.settingsLook ? ScreenTools.hairline : 1 }
                // Just short of opaque: that renderer also skips whatever lies under an opaque
                // item's whole-pixel bounds, which the scaled rule no longer covers
                opacity:    control.settingsLook ? 0.999 : 1
                color:      control.defaultBorderColor
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
            y:                  showBorder ? _verticalMargins : 0
            width:              parent.width - (showBorder ? _margins * 2 : 0)
            spacing:            _verticalMargins * (showDividers ? 2 : 1)
        }
    }

    // A hand-written settings row is a stock Labelled* row, a row led by its label and ending in
    // a button (a link, a tile set), or a switch drawing its own label; each gets the generated
    // rows' label and control sizes. SettingsRow styles itself
    Instantiator {
        model: settingsLook ? Array.from(_contentLayout.children) : []

        delegate: SettingsControlStyle {
            required property var modelData
            row: modelData
        }
    }
}
