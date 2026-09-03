import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// 하단 상태 밴드의 윗줄. 임무 전체 숫자만 한 줄로 보여준다.
// 선택 웨이포인트 상세(고도차/방위각/경사/기수)와 최대 텔레메트리 거리는 현장 판단에 쓰이지 않아
// 뺐고, 대신 고도 제한 확인에 필요한 최대 고도를 넣었다.
Rectangle {
    required property var planMasterController

    id: missionStats
    implicitHeight: ScreenTools.defaultFontPixelHeight * 2.25 // 약 36px
    color: Qt.rgba(_windowColor.r, _windowColor.g, _windowColor.b, 0.8)

    property var    _planMasterController:  planMasterController
    property color  _windowColor:           QGroundControl.globalPalette.window
    property bool   _controllerValid:       _planMasterController !== undefined && _planMasterController !== null
    property var    _missionController:     _controllerValid ? _planMasterController.missionController : undefined
    property var    _missionItems:          _controllerValid ? _missionController.visualItems : undefined
    property bool   _missionValid:          _missionItems !== undefined && _missionItems !== null

    property real   _missionPlannedDistance:    _missionValid ? _missionController.missionPlannedDistance : NaN
    property real   _missionTime:               _missionValid ? _missionController.missionTime : 0
    property int    _waypointCount:             _missionValid ? Math.max(_missionItems.count - 1, 0) : 0 // 0번 항목은 홈 위치
    property int    _batteriesRequired:         _controllerValid ? _missionController.batteriesRequired : -1

    // 최대 고도는 홈 기준 상대고도로 보여준다. 항목이 없거나 홈이 아직 정해지지 않으면 NaN 이 된다.
    property real _maxRelAltitude: {
        if (!_missionValid || _waypointCount === 0) {
            return NaN
        }
        var maxAMSL = _missionController.maxAMSLAltitude
        var homeAMSL = _missionController.plannedHomePosition.altitude
        return (isNaN(maxAMSL) || isNaN(homeAMSL)) ? NaN : maxAMSL - homeAMSL
    }

    property string _missionPlannedDistanceText: isNaN(_missionPlannedDistance) ?
                                                     _noValueText :
                                                     QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(_missionPlannedDistance).toFixed(0)
    property string _maxRelAltitudeText:         isNaN(_maxRelAltitude) ?
                                                     _noValueText :
                                                     QGroundControl.unitsConversion.metersToAppSettingsVerticalDistanceUnits(_maxRelAltitude).toFixed(0)

    readonly property string _noValueText:  "-.-"
    readonly property real   _margins:      ScreenTools.defaultFontPixelWidth
    readonly property real   _itemSpacing:  ScreenTools.defaultFontPixelWidth * 0.4

    // 7인치 터치 기준 숫자 약 18px, 라벨 약 12px. 기기 폰트 배율 설정을 따라가도록 기본 크기 비율로 잡는다.
    readonly property real _valuePointSize: ScreenTools.defaultFontPointSize * 1.125
    readonly property real _labelPointSize: ScreenTools.smallFontPointSize

    function missionTimeText() {
        var totalSeconds = Math.round(Number(_missionTime))
        if (!totalSeconds) {
            totalSeconds = 0
        }
        var hours = Math.floor(totalSeconds / 3600)
        var minutes = Math.floor((totalSeconds % 3600) / 60)
        var seconds = totalSeconds % 60
        var pad = function(value) { return value < 10 ? "0" + value : String(value) }
        return (hours > 0 ? pad(hours) + ":" : "") + pad(minutes) + ":" + pad(seconds)
    }

    RowLayout {
        anchors.leftMargin:     _margins
        anchors.rightMargin:    _margins
        anchors.left:           parent.left
        anchors.right:          parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing:                ScreenTools.defaultFontPixelWidth

        component Stat: RowLayout {
            property string label
            property string value
            property string unit
            property bool   first: false

            spacing: missionStats._itemSpacing

            QGCLabel {
                text:               "\u00b7"
                opacity:            0.5
                font.pointSize:     missionStats._labelPointSize
                visible:            !parent.first
                Layout.rightMargin: missionStats._itemSpacing
            }

            QGCLabel {
                text:           parent.label
                font.pointSize: missionStats._labelPointSize
            }

            QGCLabel {
                text:           parent.value
                font.family:    ScreenTools.fixedFontFamily
                font.pointSize: missionStats._valuePointSize
            }

            QGCLabel {
                text:           parent.unit
                font.pointSize: missionStats._labelPointSize
                visible:        parent.unit !== ""
            }
        }

        Stat {
            first: true
            label: qsTr("Total Distance")
            value: _missionPlannedDistanceText
            unit:  QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString
        }

        Stat {
            label: qsTr("Est. Time")
            value: missionTimeText()
        }

        Stat {
            // visualItems.count - 1 은 이륙·복귀·속도변경 같은 비좌표 항목까지 센다.
            // 복합 항목(서베이 등)은 내부 웨이포인트가 몇 개든 1 로 세어진다. 그래서 "항목" 이다.
            label: qsTr("Items")
            value: _waypointCount.toString()
            unit:  qsTr("ea", "count unit, as in 5 ea")
        }

        Stat {
            label: qsTr("Max Altitude")
            value: _maxRelAltitudeText
            unit:  QGroundControl.unitsConversion.appSettingsVerticalDistanceUnitsString
        }

        Stat {
            label:   qsTr("Battery")
            value:   _batteriesRequired.toString()
            unit:    qsTr("ea", "count unit, as in 5 ea")
            visible: _batteriesRequired >= 0
        }

        Item { Layout.fillWidth: true }
    }
}
