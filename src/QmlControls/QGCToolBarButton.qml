import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

// Important Note: Toolbar buttons must manage their checked state manually in order to support
// view switch prevention. This means they can't be checkable or autoExclusive.

Button {
    id:                 button
    height:             ScreenTools.defaultFontPixelHeight * 3
    leftPadding:        _horizontalMargin
    rightPadding:       _horizontalMargin
    checkable:          false

    property bool logo: false
    property real iconAspectRatio: 1

    // Bars that paint their own ground need to say how their glyphs are tinted; the default is
    // the palette's, so every existing caller is unaffected.
    property color iconColor: button.checked ? qgcPal.buttonHighlightText : qgcPal.buttonText

    // Wide wordmark logos override this smaller: their width is derived from the height via
    // iconAspectRatio, so the default icon height would make them span much of the toolbar.
    property real iconHeight: ScreenTools.defaultFontPixelHeight * 2

    property real _horizontalMargin: ScreenTools.defaultFontPixelWidth

    onCheckedChanged: checkable = false

    background: Rectangle {
        anchors.fill:   parent
        color:          button.checked ? qgcPal.buttonHighlight : Qt.rgba(0,0,0,0)
        border.color:   "red"
        border.width:   QGroundControl.corePlugin.showTouchAreas ? 3 : 0
    }

    contentItem: Row {
        spacing:                ScreenTools.defaultFontPixelWidth
        anchors.verticalCenter: button.verticalCenter
        // Logo buttons render the multi-color SVG natively via VectorImage; non-logo buttons
        // tint their monochrome icon through QGCColoredImage. Plain `Row` skips visible:false items.
        QGCVectorImage {
            visible:                button.logo
            height:                 button.iconHeight
            width:                  height * button.iconAspectRatio
            source:                 visible ? button.icon.source : ""
            anchors.verticalCenter: parent.verticalCenter
        }
        QGCColoredImage {
            visible:                !button.logo
            height:                 button.iconHeight
            width:                  height * button.iconAspectRatio
            sourceSize.height:      parent.height
            fillMode:               Image.PreserveAspectFit
            color:                  button.iconColor
            source:                 visible ? button.icon.source : ""
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            id:                     _label
            visible:                text !== ""
            text:                   button.text
            color:                  button.iconColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
