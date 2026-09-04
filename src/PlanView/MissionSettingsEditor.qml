import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.PlanView
import QGroundControl.FactControls

// Editor for Initial Camera Settings (mission item 0)
Rectangle {
    id: root
    width: availableWidth
    height: valuesColumn.height + (_margin * 2)
    // 편집기 배경을 패널 바닥과 한 톤으로 둔다. windowShadeDark 를 쓰면 상자 안의 상자가 되고,
    // 그 색을 팔레트에서 바꾸면 분석 화면·설정 화면까지 따라 바뀐다.
    color: qgcPal.window
    // 둥근 모서리는 소비자 앱 인상을 준다. 계기판처럼 각지게 둔다.
    radius: 0

    required property var missionItem
    required property real availableWidth

    property var _masterController: missionItem.masterController
    property var _controllerVehicle: _masterController.controllerVehicle
    property bool _waypointsOnlyMode: QGroundControl.corePlugin.options.missionWaypointsOnly
    property bool _showCameraSection: _waypointsOnlyMode || QGroundControl.corePlugin.showAdvancedUI
    property bool _simpleMissionStart: QGroundControl.corePlugin.options.showSimpleMissionStart

    readonly property real _margin: ScreenTools.defaultFontPixelWidth / 2

    QGCPalette { id: qgcPal }

    ColumnLayout {
        id: valuesColumn
        anchors.margins: _margin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: ScreenTools.defaultFontPixelHeight / 2

        Column {
            Layout.fillWidth: true
            spacing: _margin
            visible: !_simpleMissionStart

            CameraSection {
                id: cameraSection
                anchors.left: parent.left
                anchors.right: parent.right
                missionItem: root.missionItem
                visible: _showCameraSection
                showSectionHeader: false

                Component.onCompleted: checked = !_waypointsOnlyMode && missionItem.cameraSection.settingsSpecified
            }

            QGCLabel {
                anchors.left: parent.left
                anchors.right: parent.right
                text: qsTr("Above camera commands will take affect immediately upon mission start.")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                font.pointSize: ScreenTools.smallFontPointSize
                visible: _showCameraSection && cameraSection.checked
            }
        }
    }
}
