import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// 임무 중 자동 녹화 줄. 계획 목록에서 임무 시작(초기 카메라 설정) 바로 밑에 따로 한 줄로 놓인다.
/// 임무 시작을 펼치지 않아도 보인다. 환경설정 영상 화면 카메라 설정의 같은 값이고,
/// 줄과 스위치도 그 화면 것을 쓴다.
Item {
    id: root
    implicitHeight: _gap + panel.height

    readonly property bool settingsMockupLook: true

    // 목록의 행 사이 간격(rowSpacing)만큼 띄워 위 행과 다른 줄로 읽히게 한다
    readonly property real _gap:    2
    readonly property var  _fact:   QGroundControl.settingsManager.siyiCameraSettings.autoRecordMission

    QGCPalette { id: qgcPal }

    Rectangle {
        id:     panel
        y:      root._gap
        width:  root.width
        height: row.height + ScreenTools.defaultFontPixelHeight
        color:  qgcPal.window

        SettingsRow {
            id:                     row
            anchors.left:           parent.left
            anchors.right:          parent.right
            // 왼쪽은 위 행의 글자와 줄을 맞춘다(MissionItemEditor 의 _margin)
            anchors.leftMargin:     ScreenTools.defaultFontPixelWidth / 2
            anchors.rightMargin:    ScreenTools.defaultFontPixelWidth
            anchors.verticalCenter: parent.verticalCenter
            label:                  root._fact.label
            description:            root._fact.shortDescription
            controlFillsRow:        true

            FactCheckBoxSlider {
                objectName:         "missionSettings_autoRecordMission"
                Layout.fillWidth:   true
                text:               ""
                fact:               root._fact
                enabled:            QGroundControl.settingsManager.siyiCameraSettings.enabled.rawValue
            }
        }
    }
}
