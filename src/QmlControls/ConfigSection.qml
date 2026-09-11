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

    // The icon rides beside the heading rather than inside the card. It used to hold a column
    // of its own down the card's left edge, which pushed every row in an illustrated section
    // right by the icon's width - so on a page mixing illustrated and plain sections the rows
    // started at a different place card by card. Up here it costs the rows nothing, and the
    // heading still starts at the same left edge whether a section has artwork or not.
    RowLayout {
        Layout.fillWidth: true
        spacing: ScreenTools.defaultFontPixelWidth
        visible: control.heading !== "" || control.iconSource !== ""

        QGCLabel {
            text: control.heading
            font.pointSize: ScreenTools.defaultFontPointSize * 0.85
            font.bold: true
            opacity: 0.6
            visible: control.heading !== ""
        }

        // Sized off the heading's own line and free to keep its aspect: the artwork is wider
        // than tall in some sections and taller than wide in others, and a fixed box
        // letterboxed the tall ones into looking indented.
        QGCColoredImage {
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight
            Layout.preferredWidth: Layout.preferredHeight * 2
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignLeft
            source: control.iconSource
            color: qgcPal.text
            opacity: 0.6
            visible: control.iconSource !== ""
        }

        Item { Layout.fillWidth: true }
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
