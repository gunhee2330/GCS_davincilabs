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
        border.color: "#ff3b30"
        border.width: Math.max(1, ScreenTools.defaultFontPixelWidth * 0.2)

        Rectangle {
            anchors.left:   parent.left
            anchors.bottom: parent.top
            height:         targetText.implicitHeight + 8
            width:          targetText.implicitWidth + 14
            color:          "#d9b00000"

            Text {
                id:             targetText
                anchors.centerIn: parent
                color:          "white"
                font.bold:      true
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.62
                text:           root.targetLabel
            }
        }
    }
}
