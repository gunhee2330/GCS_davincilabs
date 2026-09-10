import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

ColumnLayout {
    id: control
    spacing: ScreenTools.defaultFontPixelHeight / 3

    default property alias contentItem: _controlsColumn.data

    property string heading
    property string iconSource

    property real _margins: ScreenTools.defaultFontPixelHeight * 0.83
    // Half of it lands above each row and half below, giving every row the same generous
    // touch band and leaving the hairline centred in the gap
    property real _rowSpacing: ScreenTools.defaultFontPixelHeight * 1.44

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCLabel {
        text: control.heading
        font.pointSize: ScreenTools.defaultFontPointSize * 0.85
        font.bold: true
        opacity: 0.6
        visible: control.heading !== ""
    }

    Rectangle {
        id: _card
        implicitWidth: _contentRow.implicitWidth + _margins * 2
        implicitHeight: _contentRow.implicitHeight + _margins * 2
        Layout.fillWidth: true
        color: qgcPal.button
        border.width: 1
        border.color: qgcPal.groupBorder
        radius: ScreenTools.defaultFontPixelHeight / 2

        RowLayout {
            id: _contentRow
            y: _margins
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: _margins
            anchors.rightMargin: _margins
            spacing: ScreenTools.defaultFontPixelWidth * 2

            QGCColoredImage {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 3
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                source: control.iconSource
                color: qgcPal.text
                fillMode: Image.PreserveAspectFit
                visible: control.iconSource !== ""
            }

            ColumnLayout {
                id: _controlsColumn
                Layout.fillWidth: true
                spacing: _rowSpacing
            }
        }

        // Row separators. Parented to the card rather than to _controlsColumn, otherwise the
        // repeater would be feeding itself the children it iterates over.
        Repeater {
            model: _controlsColumn.children.length

            Rectangle {
                required property int index
                // Delegates outlive a shrinking child list for one binding pass, so the
                // lookup can miss before the repeater re-models
                readonly property Item row: index < _controlsColumn.children.length
                                                ? _controlsColumn.children[index] : null

                x: _contentRow.x + _controlsColumn.x
                width: _controlsColumn.width
                y: _contentRow.y + _controlsColumn.y + (row ? row.y : 0) - _rowSpacing / 2
                height: 1
                color: qgcPal.groupBorder
                // row.y > 0 means some visible row precedes this one; a hidden first row
                // collapses the layout so its successor must not draw a leading rule
                visible: row && row.visible && row.height > 0 && row.y > 0
            }
        }
    }
}
