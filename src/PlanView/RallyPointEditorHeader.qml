import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:     outerEditorRect
    height: innerEditorRect.y + innerEditorRect.height + (_margin * 2)
    radius: _radius
    color:  qgcPal.missionItemEditor

    property var controller ///< RallyPointController

    readonly property real  _margin: ScreenTools.defaultFontPixelWidth / 2
    readonly property real  _radius: ScreenTools.defaultFontPixelWidth / 2

    QGCLabel {
        id:                 editorLabel
        anchors.margins:    _margin
        anchors.left:       parent.left
        anchors.top:        parent.top
        text:               qsTr("Rally Points")
    }

    Rectangle {
        id:                 innerEditorRect
        anchors.margins:    _margin
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        editorLabel.bottom
        height:             infoLabel.height + (_margin * 2)
        // 편집기 배경을 패널 바닥과 한 톤으로 둔다. windowShadeDark 를 쓰면 상자 안의 상자가 되고,
        // 그 색을 팔레트에서 바꾸면 분석 화면·설정 화면까지 따라 바뀐다.
        color:              qgcPal.window
        radius:             _radius

        QGCLabel {
            id:                 infoLabel
            anchors.margins:    _margin
            anchors.top:        parent.top
            anchors.left:       parent.left
            anchors.right:      parent.right
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            text:               qsTr("Rally Points provide alternate landing points when performing a Return to Launch (RTL).")
        }
    }
}
