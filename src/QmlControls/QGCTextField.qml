import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

TextField {
    id:                 control
    color:              qgcPal.textFieldText
    // Left unset this falls back to the Qt system palette, which read fine only while the dark
    // field was white. The field is now the darkest surface on screen, so the placeholder has to
    // come from the palette too - buttonBorder is the same muted ink as the field border and
    // clears 3:1 on the field in both themes
    placeholderTextColor: qgcPal.buttonBorder
    selectionColor:     qgcPal.textFieldText
    selectedTextColor:  qgcPal.textField
    activeFocusOnPress: true
    antialiasing:       true
    font.pointSize:     ScreenTools.defaultFontPointSize
    font.family:        ScreenTools.normalFontFamily
    inputMethodHints:   numericValuesOnly && !ScreenTools.isiOS ?
                            Qt.ImhFormattedNumbersOnly:  // Forces use of virtual numeric keyboard instead of full keyboard
                            Qt.ImhNone                   // iOS numeric keyboard has no done button, we can't use it.
    leftPadding:        _marginPadding
    rightPadding:       _marginPadding + unitsHelpLayout.width
    topPadding:         _verticalPadding
    bottomPadding:      _verticalPadding
    // The mockup right-aligns the numeric field against its units; free text
    // still reads left to right
    horizontalAlignment: numericValuesOnly ? TextInput.AlignRight : TextInput.AlignLeft
    EnterKey.type:      Qt.EnterKeyDone

    property bool   showUnits:          false
    property bool   showHelp:           false
    property string unitsLabel:         ""
    property string extraUnitsLabel:    ""
    property bool   numericValuesOnly:  false   // true: Used as hint for mobile devices to show numeric only keyboard
    property alias  textColor:          control.color
    property bool   validationError:    false

    property real _helpLayoutWidth: 0
    // Mockup field padding is 12 across and 8 down against its 18px text metric
    property real _marginPadding:   ScreenTools.defaultFontPixelHeight * 0.67
    property real _verticalPadding: ScreenTools.defaultFontPixelHeight * 0.44

    signal helpClicked

    Component.onCompleted: checkActiveFocus()
    onActiveFocusChanged: checkActiveFocus()

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    onEditingFinished: {
        if (ScreenTools.isMobile) {
            // Toss focus on mobile after Done on virtual keyboard. Prevent strange interactions.
            focus = false
        }
    }

    function checkActiveFocus() {
        if (activeFocus) {
            selectAll()
            if (validationError) {
                validationToolTip.visible = true
            }
        } else {
            validationToolTip.visible = false
        }
    }

    function showValidationError(errorString, originalValidValue = undefined, preventViewSiwtch = true) {
        validationToolTip.text = errorString
        validationToolTip.originalValidValue = originalValidValue
        validationToolTip.visible = true
        if (!validationError) {
            validationError = true
            if (preventViewSiwtch) {
                globals.validationErrorCount++
            }
        }
    }

    function clearValidationError(preventViewSiwtch = true) {
        validationToolTip.visible = false
        validationToolTip.originalValidValue = undefined
        if (validationError) {
            validationError = false
            if (preventViewSiwtch) {
                globals.validationErrorCount--
            }
        }
    }

    background: Rectangle {
        // The dark theme's field fill is darker than the surfaces around it, so
        // the border is what separates the two - draw it in both themes
        border.width:   control.validationError ? 2 : 1
        border.color:   control.validationError ? qgcPal.colorRed : qgcPal.buttonBorder
        // Mockup corner is 7 against its 18px text metric
        radius:         ScreenTools.defaultFontPixelHeight * 0.39
        color:          qgcPal.textField
        // The mockup's 92 floor is what makes room for the wider padding above
        implicitWidth:  Math.max(ScreenTools.implicitTextFieldWidth, ScreenTools.defaultFontPixelHeight * 5.1)
        implicitHeight: ScreenTools.implicitTextFieldHeight

        RowLayout {
            id:                     unitsHelpLayout
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.right:          parent.right
            anchors.rightMargin:    control.activeFocus ? 2 : control._marginPadding
            spacing:                ScreenTools.defaultFontPixelWidth / 4
            layoutDirection:        Qt.RightToLeft

            Component.onCompleted:  control._helpLayoutWidth = unitsHelpLayout.width
            onWidthChanged:         control._helpLayoutWidth = unitsHelpLayout.width

            // Help button
            Rectangle {
                id:                     helpButton
                Layout.margins:         2
                Layout.leftMargin:      0
                Layout.rightMargin:     1
                Layout.fillHeight:      true
                Layout.preferredWidth:  helpLabel.contentWidth * 3
                Layout.alignment:       Qt.AlignVCenter
                color:                  control.color
                visible:                control.showHelp && control.activeFocus

                QGCLabel {
                    id:                 helpLabel
                    anchors.centerIn:   parent
                    color:              qgcPal.textField
                    text:               qsTr("?")
                }

            }

            // Extra units
            Text {
                Layout.alignment:   Qt.AlignVCenter
                text:               control.extraUnitsLabel
                font.pointSize:     ScreenTools.smallFontPointSize
                font.family:        ScreenTools.normalFontFamily
                antialiasing:       true
                color:              control.color
                visible:            control.showUnits && text !== ""
            }

            // Units
            Text {
                Layout.alignment:   Qt.AlignVCenter
                // Right aligning the value parks it against the units, so the units need their
                // own gap or "15.0" and "m" run together. The layout is right to left, so this
                // margin is the space between the value and the unit
                Layout.leftMargin:  ScreenTools.defaultFontPixelWidth
                text:               control.unitsLabel
                font.pointSize:     control.activeFocus ? ScreenTools.smallFontPointSize : ScreenTools.defaultFontPointSize
                font.family:        ScreenTools.normalFontFamily
                antialiasing:       true
                color:              control.color
                visible:            control.showUnits && text !== ""
            }
        }
    }

    ToolTip {
        id: validationToolTip

        property var originalValidValue: undefined

        QGCMouseArea {
            anchors.fill: parent
            onClicked: {
                if (validationToolTip.originalValidValue !== undefined) {
                    control.text = validationToolTip.originalValidValue
                    control.clearValidationError()
                }
            }
        }
    }

    MouseArea {
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        anchors.right:  parent.right
        width:          control._helpLayoutWidth
        enabled:        helpButton.visible
        onClicked:      control.helpClicked()
    }
}
