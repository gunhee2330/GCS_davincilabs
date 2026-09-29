import QtQuick

import QGroundControl.Controls

Item {
    id: root

    property bool targetVisible: false
    property real targetX:       0
    property real targetY:       0
    property real targetWidth:   0
    property real targetHeight:  0
    property real sourceWidth:   1280
    property real sourceHeight:  720
    property string targetLabel: qsTr("TARGET")
    property int fillMode:       Image.PreserveAspectCrop
    // Box and label colour by the module's class word. A class the bench confirms is one more
    // line here; anything not listed is drawn as object.
    readonly property var _classColours: ({
        "person": { box: "#e0a800", text: "black" },
        "car":    { box: "#a78bfa", text: "black" },
        "object": { box: "#ff9500", text: "white" },
        "boat":   { box: "#1f9fd0", text: "white" },
        "smoke":  { box: "#9aa5b1", text: "black" },
        "fire":   { box: "#ff5b3a", text: "white" }
    })
    readonly property var _classColour: _classColours[targetLabel] || _classColours["object"]
    readonly property color trackedColor: _classColour.box

    readonly property real _scaleX: fillMode === Image.Stretch ? width / sourceWidth
                                                                : fillMode === Image.PreserveAspectFit
                                                                  ? Math.min(width / sourceWidth, height / sourceHeight)
                                                                  : Math.max(width / sourceWidth, height / sourceHeight)
    readonly property real _scaleY: fillMode === Image.Stretch ? height / sourceHeight : _scaleX
    readonly property real _contentWidth:  sourceWidth * _scaleX
    readonly property real _contentHeight: sourceHeight * _scaleY
    readonly property real _offsetX:       (width - _contentWidth) / 2
    readonly property real _offsetY:       (height - _contentHeight) / 2

    visible: targetVisible && sourceWidth > 0 && sourceHeight > 0 && targetWidth > 0 && targetHeight > 0

    Rectangle {
        x:           root._offsetX + root.targetX * root._scaleX
        y:           root._offsetY + root.targetY * root._scaleY
        width:       root.targetWidth * root._scaleX
        height:      root.targetHeight * root._scaleY
        color:       "transparent"
        // The same box the detector drew, in the colour that means "this one is being followed":
        // the operator picked a green box and watches it turn, rather than gaining a second mark.
        border.color: root.trackedColor
        border.width: Math.max(1, ScreenTools.defaultFontPixelWidth * 0.2)

        Rectangle {
            anchors.left:   parent.left
            anchors.bottom: parent.top
            height:         targetText.implicitHeight + 8
            width:          targetText.implicitWidth + 14
            color:          root.trackedColor

            Text {
                id:             targetText
                anchors.centerIn: parent
                color:          root._classColour.text
                font.bold:      true
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.62
                text:           root.targetLabel
            }
        }
    }
}
