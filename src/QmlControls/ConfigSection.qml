import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

ColumnLayout {
    id: control
    // Caption to card, .45 cqw in the police mockup
    spacing: ScreenTools.mockupUnit * 0.45

    default property alias contentItem: _controlsColumn.data

    property string heading
    property string iconSource

    // The settings cards' padding, after the police mockup: 1.4 cqw across, 1 cqw down
    property real _margins: ScreenTools.mockupUnit * 1.4
    property real _verticalMargins: ScreenTools.mockupUnit
    // Half of it lands above each row and half below, the mockup's row padding pair, leaving
    // the hairline centred in the gap
    property real _rowSpacing: _verticalMargins * 2

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
            id: _headingLabel
            text: control.heading
            font.pointSize: ScreenTools.mockupPointUnit * 0.95
            font.letterSpacing: ScreenTools.mockupUnit * 0.019    // .02em of the caption
            color: qgcPal.secondaryText
            visible: control.heading !== ""
        }

        // Sized off the heading's own line and free to keep its aspect: the artwork is wider
        // than tall in some sections and taller than wide in others, and a fixed box
        // letterboxed the tall ones into looking indented.
        QGCColoredImage {
            Layout.preferredHeight: _headingLabel.implicitHeight
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
        implicitHeight: _contentRow.implicitHeight + _verticalMargins * 2
        Layout.fillWidth: true
        color: qgcPal.card
        border.width: ScreenTools.hairline
        border.pixelAligned: false    // a whole-pixel snap would round the hairline away
        border.color: qgcPal.cardBorder
        radius: ScreenTools.mockupUnit * 0.6

        RowLayout {
            id: _contentRow
            y: _verticalMargins
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

                // Edge to edge inside the card border, as the mockup rules its rows
                x: ScreenTools.hairline
                width: _card.width - ScreenTools.hairline * 2
                y: _contentRow.y + _controlsColumn.y + (row ? row.y : 0) - _rowSpacing / 2
                height: 1
                // A fill this thin is rounded up to a whole logical pixel by the software renderer;
                // one scaled down from a whole pixel lands on a single device pixel
                transform: Scale { yScale: ScreenTools.hairline }
                // Just short of opaque: that renderer also skips whatever lies under an opaque
                // item's whole-pixel bounds, which the scaled rule no longer covers
                opacity: 0.999
                color: qgcPal.cardBorder
                // row.y > 0 means some visible row precedes this one; a hidden first row
                // collapses the layout so its successor must not draw a leading rule
                visible: row && row.visible && row.height > 0 && row.y > 0
            }
        }
    }

    // The settings cards' row type and control sizes, on the stock rows the vehicle pages are
    // built from
    Instantiator {
        model: Array.from(_controlsColumn.children)

        delegate: SettingsControlStyle {
            required property var modelData
            row: modelData
        }
    }
}
