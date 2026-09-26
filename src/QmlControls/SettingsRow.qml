import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// A generated settings row laid out after the police mockup: the label with its description
/// under it on the left, the control on the right, both centred on the row. The control is the
/// stock one with its own label left blank, so its kind, binding and behaviour are unchanged.
Item {
    id: root

    property string label
    property string description
    /// A switch spans the row under the label, so the whole row still toggles it the way it did
    /// while the switch drew its own label
    property bool   controlFillsRow:        false
    /// Width for a control that has none of its own (the slider)
    property real   controlPreferredWidth:  -1

    default property alias controlData: _controlSlot.data

    Layout.fillWidth:   true
    // Room for the label beside the control; the description wraps into whatever is left
    implicitWidth:      ScreenTools.mockupUnit * 20 + _gap + _controlWidth
    // Mockup rows are 4.2 cqw at least, of which the card's padding pair takes 2
    implicitHeight:     Math.max(_labelColumn.implicitHeight, _controlSlot.implicitHeight, ScreenTools.mockupUnit * 2.2)

    readonly property real  _gap:           ScreenTools.mockupUnit * 1.5
    // A control that wants more (a long browse path) gives way to the label
    readonly property real  _maxControlWidth: ScreenTools.mockupUnit * 40
    readonly property Item  _control:       _controlSlot.children.length > 0 ? _controlSlot.children[0] : null
    readonly property real  _controlWidth:  controlFillsRow ? (_control ? _control.implicitWidth : 0) : _controlSlot.width

    QGCPalette { id: qgcPal; colorGroupEnabled: _labelColumn.enabled }

    ColumnLayout {
        id:                     _labelColumn
        anchors.left:           parent.left
        anchors.right:          parent.right
        anchors.rightMargin:    root._controlWidth + root._gap
        anchors.verticalCenter: parent.verticalCenter
        spacing:                ScreenTools.mockupUnit * 0.15
        // Dims with the control, as the label did while the control drew it
        enabled:                root._control ? root._control.enabled : true

        // Sized by the style below, like a hand-written row's own label
        QGCLabel {
            id:                 _labelText
            Layout.fillWidth:   true
            text:               root.label
            wrapMode:           Text.WordWrap
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               root.description
            visible:            text !== ""
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.mockupPointUnit * 0.9
            color:              qgcPal.secondaryText
        }
    }

    RowLayout {
        id:                     _controlSlot
        anchors.right:          parent.right
        anchors.verticalCenter: parent.verticalCenter
        width:                  root.controlFillsRow ? parent.width : (root.controlPreferredWidth > 0 ? root.controlPreferredWidth : Math.min(implicitWidth, root._maxControlWidth))
    }

    // The mockup's control sizes on this row's control only
    SettingsControlStyle { id: _style; control: root._control; labelItem: _labelText }

    // The blanked label still holds its spacing ahead of a combo box
    Binding { when: !!_style._comboBox; target: root._control; property: "spacing"; value: 0 }
}
