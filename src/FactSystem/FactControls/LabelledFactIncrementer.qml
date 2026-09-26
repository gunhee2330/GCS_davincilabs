import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Generic increment/decrement control for a numeric Fact.
/// Shows a label, a "-" button, the current value with its units,
/// and a "+" button.  Clamped to fact.min / fact.max.
///
/// Properties:
///   fact    - The Fact to control (required).
///   label   - Display label (defaults to fact.label).
///   step    - Amount added/subtracted per click (default 5).
RowLayout {
    id:      root

    property string label:      fact.label
    property Fact   fact
    property real   step:       5

    spacing: ScreenTools.defaultFontPixelWidth * 2

    QGCLabel {
        Layout.fillWidth:    true
        Layout.minimumWidth: implicitWidth
        text:                root.label
    }

    // The police mockup's stepper: one bordered group, the two buttons filled and the value
    // between them, each padded .35 cqw down and .9 cqw across in 1.15 cqw text. The group
    // carries the button fill, so only its outer corners round; the value cell covers the middle
    Rectangle {
        id:             _stepper
        implicitWidth:  _stepRow.implicitWidth + ScreenTools.hairline * 2
        implicitHeight: _stepRow.implicitHeight + ScreenTools.hairline * 2
        color:          qgcPal.stepperFill
        border.width:   ScreenTools.hairline
        border.pixelAligned: false    // a whole-pixel snap would round the hairline away
        border.color:   qgcPal.cardBorder
        radius:         _radius

        property real _radius:      ScreenTools.mockupUnit * 0.4
        property real _padV:        ScreenTools.mockupUnit * 0.35
        property real _padH:        ScreenTools.mockupUnit * 0.9
        property real _pointSize:   ScreenTools.mockupPointUnit * 1.15
        // Both end cells as wide as the wider glyph, so they match
        property real _buttonWidth: Math.max(minusButton.implicitContentWidth, plusButton.implicitContentWidth) + _padH * 2

        QGCPalette { id: qgcPal; colorGroupEnabled: root.enabled }

        RowLayout {
            id:         _stepRow
            x:          ScreenTools.hairline
            y:          ScreenTools.hairline
            spacing:    0

            QGCButton {
                id:                 minusButton
                implicitWidth:      _stepper._buttonWidth
                implicitHeight:     implicitContentHeight + topPadding + bottomPadding
                topPadding:         _stepper._padV
                bottomPadding:      _stepper._padV
                leftPadding:        _stepper._padH
                rightPadding:       _stepper._padH
                pointSize:          _stepper._pointSize
                showBorder:         false
                backRadius:         _stepper._radius
                backgroundColor:    "transparent"
                text:               "-"
                onClicked: {
                    if (root.fact.value > root.fact.min) {
                        root.fact.value = root.fact.value - root.step
                    }
                }
            }

            QGCLabel {
                id:                 valueLabel
                leftPadding:        _stepper._padH
                rightPadding:       _stepper._padH
                font.pointSize:     _stepper._pointSize
                text:               root.fact.valueString

                // The row's full height, not just the text line
                Rectangle {
                    y:      -parent.y
                    width:  parent.width
                    height: _stepRow.height
                    z:      -1
                    color:  qgcPal.card
                }
            }

            QGCButton {
                id:                 plusButton
                implicitWidth:      _stepper._buttonWidth
                implicitHeight:     implicitContentHeight + topPadding + bottomPadding
                topPadding:         _stepper._padV
                bottomPadding:      _stepper._padV
                leftPadding:        _stepper._padH
                rightPadding:       _stepper._padH
                pointSize:          _stepper._pointSize
                showBorder:         false
                backRadius:         _stepper._radius
                backgroundColor:    "transparent"
                text:               "+"
                onClicked: {
                    if (root.fact.value < root.fact.max) {
                        root.fact.value = root.fact.value + root.step
                    }
                }
            }
        }
    }
}
