import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// The police settings mockup's row type and row layout, laid onto one settings row as it stands.
/// The controls draw the mockup's own boxes under the settings views themselves; this sets what
/// depends on the row: its label and value text, its height, a combo box's width, and a slider
/// field set on one line. The row is a stock one: a Labelled* wrapper and the combo box, text
/// field or button inside it, a switch, or a layout holding a slider field and a button. Each part
/// is recognised by a property of its own.
Item {
    id: root
    visible: false

    /// A card's child to recognise and style whole: a Labelled* row, a row led by its label (or
    /// by a Labelled* control) and ending in a button (a link, a tile set), a combo box or a text
    /// field, or holding just a value, a switch drawing its own label, a FactTextFieldSlider, a
    /// lone button or a line of text. Anything else, a SettingsRow included, is left alone
    property Item row

    /// The row's control, or one set here directly
    property Item control:      _rowIsSwitch || _rowIsLabelled || _rowIsFieldSlider || _rowIsButton || _rowIsText ? row : null
    /// The row's own label, where the stock row still draws it
    property Item labelItem:    _rowIsSwitch ? row.contentItem.children[0] :
                                _rowIsLabelled ? _leadLabel :
                                _rowIsFieldSlider ? _fieldSliderLabel : null
    /// The value text beside it (an info row, a tile set's size), or a line of text on its own
    property Item valueItem:    _rowIsLabelled && _isText(row.children[1]) ? row.children[1] :
                                _rowIsText ? row : null

    readonly property bool _rowIsSwitch:    !!row && row._trackHeight !== undefined && row.text !== ""
    readonly property bool _rowIsButton:    !!row && row.heightFactor !== undefined
    readonly property bool _rowIsText:      _isText(row)
    // Its label is the enable check box's text, or the text field's own label without one
    readonly property bool _rowIsFieldSlider: !!row && row.showEnableCheckbox !== undefined && row.textField !== undefined
    readonly property Item _fieldSliderLabel: !_rowIsFieldSlider ? null :
                                              (row.showEnableCheckbox ? row.children[0]?.children[0]?.children[0]?.contentItem ?? null
                                                                      : row.textField.children[0] ?? null)
    // The row's own label, or that of the Labelled* control leading it
    readonly property Item _leadLabel:      !row || row.controlFillsRow !== undefined || row.children.length < 2 ? null :
                                            _isText(row.children[0]) ? row.children[0] :
                                            row.children[0].label !== undefined && _isText(row.children[0].children[0]) ? row.children[0].children[0] : null
    readonly property Item _lastChild:      row && row.children.length > 0 ? row.children[row.children.length - 1] : null
    readonly property bool _rowIsLabelled:  !!_leadLabel &&
                                            (row.label !== undefined || _lastChild.heightFactor !== undefined ||
                                             _lastChild.sizeToContents !== undefined || _lastChild.validationError !== undefined ||
                                             (row.children.length === 2 && _isText(row.children[1])))

    // A label, not a control (a text field has no contentItem either, but it echoes)
    function _isText(item) {
        return !!item && item.font !== undefined && item.text !== undefined && item.contentItem === undefined && item.echoMode === undefined
    }

    readonly property var _parts:       control ? [ control ].concat(Array.from(control.children)) : []
    readonly property var _comboBox:    control && control.comboBox && control.comboBox.sizeToContents !== undefined ? control.comboBox :
                                        control && control.sizeToContents !== undefined ? control : null
    // FactTextFieldSlider's textField is one more wrapper around the field
    readonly property var _textField:   {
        for (const part of _parts) {
            const field = part.textField && part.textField.textField ? part.textField.textField : part.textField
            if (field && field.validationError !== undefined) {
                return field
            }
        }
        return null
    }
    readonly property var _fieldSlider: _parts.find(part => part.showEnableCheckbox !== undefined && part.textField !== undefined) ?? null
    // With its label on the check box, or none, the slider can take the line between the two
    readonly property bool _inlineSlider: !!_fieldSlider && (_fieldSlider.showEnableCheckbox || _fieldSlider.label === "")

    QGCPalette { id: qgcPal; colorGroupEnabled: root.control ? root.control.enabled : true }

    // Row label 1.25 cqw at the mockup's 500. Open Sans ships 400 and 600 only and 500 lands on
    // 400, which reads lighter than the mockup's label, so the nearest heavier face stands in
    Binding { when: !!root.labelItem; target: root.labelItem; property: "font.pointSize"; value: ScreenTools.mockupPointUnit * 1.25 }
    Binding { when: !!root.labelItem; target: root.labelItem; property: "font.weight"; value: Font.DemiBold }
    Binding { when: !!root.valueItem; target: root.valueItem; property: "font.pointSize"; value: ScreenTools.mockupPointUnit * 1.15 }

    // Mockup rows are 4.2 cqw at least, of which the card's padding pair takes 2
    Binding { when: !!root.row && !!root.control; target: root.row; property: "Layout.minimumHeight"; value: ScreenTools.mockupUnit * 2.2 }

    // The select is as wide as its text and 9 cqw at least
    Binding { when: !!root._comboBox && root._comboBox !== root.control; target: root.control; property: "comboBoxPreferredWidth"
              value: Math.max(ScreenTools.mockupUnit * 9, root._comboBox ? root._comboBox.contentItem.implicitWidth + root._comboBox.leftPadding + root._comboBox.rightPadding + root._comboBox.padding : 0) }

    // The volume row's slider, value and button on one line, on the card itself. A slider's value
    // is plain text beside it, only as wide as the value and its units; a failed entry keeps its
    // red edge
    Binding { when: !!root._fieldSlider; target: root._fieldSlider; property: "backgroundColor"; value: "transparent" }
    Binding { when: root._inlineSlider; target: root._fieldSlider; property: "sliderInline"; value: true }
    Binding { when: root._inlineSlider && !!root._textField; target: root._textField; property: "_marginPadding"; value: ScreenTools.mockupUnit * 0.3 }
    Binding { when: root._inlineSlider && !!root._textField; target: root._textField ? root._textField.background : null; property: "border.color"
              value: root._textField && root._textField.validationError ? qgcPal.colorRed : "transparent" }
    Binding { when: root._inlineSlider && !!root._textField; target: root._textField ? root._textField.background : null; property: "implicitWidth"; value: 0 }
}
