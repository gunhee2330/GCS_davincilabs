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

    // ── 글꼴 정책 (레인 A). 배정 4파일 공통 ──────────────────────────────────
    // 1) 글꼴은 QGCLabel 기본값인 ScreenTools.normalFontFamily 한 벌만 쓴다. 한국어 로케일에서는
    //    앱에 내장된 NanumGothic 이다(ScreenToolsController::normalFontFamily).
    //    숫자에만 주던 ScreenTools.fixedFontFamily 는 내장 글꼴이 아니라 QFontDatabase 가 돌려주는
    //    OS 기본 고정폭이다. 기기마다 다르고 한글 라벨과 획 굵기·글자 높이가 어긋난다 — 이것이
    //    "글꼴이 따로 논다" 의 원인이었다.
    // 2) 고정폭을 버려도 자릿수는 흔들리지 않는다. NanumGothic 은 0~9 advance 가 모두 606/1000em 로
    //    이미 tabular 다(Open Sans 도 1171/2048em 로 동일). Qt 6.11 의 font.features 로 tnum 을 켤
    //    이유도 없다 — NanumGothic 에는 GSUB 테이블 자체가 없어 tnum 은 아무 일도 하지 않는다.
    // 3) 크기는 3단만. 기기 폰트 배율을 따라가도록 항상 기본 크기의 비율로 잡는다.
    //      캡션(단위·부제·축 눈금) = smallFontPointSize (기본 ×0.75)
    //      본문                    = defaultFontPointSize (QGCLabel 기본값이라 따로 적지 않음)
    //      강조(값·제목·커맨드명)  = defaultFontPointSize × 1.15
    // TODO: 같은 상수가 배정 4파일에 중복돼 있다. 나중에 공용 싱글턴 한 곳으로 모을 것.
    readonly property real _fontEmphasis: ScreenTools.defaultFontPointSize * 1.15
    readonly property real _fontCaption:  ScreenTools.smallFontPointSize

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
            // 항목마다 같은 폭을 가져가 밴드 전체에 고르게 퍼진다. 왼쪽에 몰리면 오른쪽이 통째로 빈다.
            Layout.fillWidth: true

            property string label
            property string value
            property string unit
            property bool   first: false

            spacing: missionStats._itemSpacing

            // 가운뎃점 문자는 잉크 높이가 0.115em 뿐이라 216ppi 패널에서 사라진다. 선으로 긋는다.
            Rectangle {
                Layout.alignment:       Qt.AlignVCenter
                Layout.preferredWidth:  1
                Layout.preferredHeight: Math.round(ScreenTools.defaultFontPixelHeight * 0.9)
                Layout.rightMargin:     missionStats._itemSpacing
                color:                  QGroundControl.globalPalette.text
                opacity:                0.28
                visible:                !parent.first
            }

            // 캡션과 강조가 한 줄에 섞이므로 기준선을 맞춘다. 세로 가운데로 두면 글자가 떠 보인다.
            QGCLabel {
                text:             parent.label
                font.pointSize:   missionStats._fontCaption
                Layout.alignment: Qt.AlignBaseline
            }

            QGCLabel {
                text:             parent.value
                font.pointSize:   missionStats._fontEmphasis
                Layout.alignment: Qt.AlignBaseline
            }

            QGCLabel {
                text:             parent.unit
                font.pointSize:   missionStats._fontCaption
                visible:          parent.unit !== ""
                Layout.alignment: Qt.AlignBaseline
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
    }
}
