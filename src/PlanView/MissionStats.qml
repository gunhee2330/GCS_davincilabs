import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// 임무 전체 요약. 우측 패널 맨 위에 고정되어 트리와 함께 스크롤되지 않는다.
// 전체 요약은 6칸 고정 격자다. 순찰 운용에서 출동 전에 확인하는 값만 넣는다 —
// 거리/시간(소티 규모), 최대 반경(통신 링크·육안 범위 한계), 최대 고도(고도 제한),
// 항목 수, 필요 배터리.
// 그 아래 선택 항목 상세(방위/이전 점 거리/경사/고도 차/기수 방향)는 순정 "Selected Waypoint" 와 같은
// 값이다. 경로 위의 점을 골랐을 때만 보인다.
Rectangle {
    required property var planMasterController

    id: missionStats
    // 전체 요약은 3행 고정이고, 선택 항목이 보일 때만 그만큼 늘어난다. 내용이 높이를 정한다.
    implicitHeight: statGrid.implicitHeight + (_margins * 2)
    color: "transparent"

    // 트리와 경계를 긋는다. 요약은 트리의 일부가 아니라 그 위에 얹힌 고정 줄이다.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: QGroundControl.globalPalette.groupBorder
        opacity: 0.5
    }

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
    // 홈에서 가장 멀어지는 거리. 통신 링크와 육안 범위가 버티는지 출동 전에 보는 값이다.
    property real   _missionMaxTelemetry:       _missionValid ? _missionController.missionMaxTelemetry : NaN

    // 선택 항목. 값과 계산은 순정 MissionStats 그대로다.
    property var    _currentMissionItem:        _controllerValid ? _missionController.currentPlanViewItem : null
    property bool   _currentMissionItemValid:   _currentMissionItem !== undefined && _currentMissionItem !== null
    property bool   _currentItemIsVTOLTakeoff:  _currentMissionItemValid && _currentMissionItem.command == 84
    // 0번(임무 시작/홈)과 경로가 지나지 않는 항목(ROI, 속도 변경 같은 명령)은 이전 점 기준 값이 없다.
    property bool   _showSelectedItem:          _currentMissionItemValid && _currentMissionItem.sequenceNumber > 0 &&
                                                _currentMissionItem.specifiesCoordinate && !_currentMissionItem.isStandaloneCoordinate
    property real   _distance:                  _currentMissionItemValid ? _currentMissionItem.distance : NaN
    property real   _altDifference:             _currentMissionItemValid ? _currentMissionItem.altDifference : NaN
    property real   _azimuth:                   _currentMissionItemValid ? _currentMissionItem.azimuth : NaN
    property real   _heading:                   _currentMissionItemValid ? _currentMissionItem.missionVehicleYaw : NaN
    property real   _gradient:                  _currentMissionItemValid && _currentMissionItem.distance > 0 ?
                                                    (_currentItemIsVTOLTakeoff ?
                                                         0 : (Math.atan(_currentMissionItem.altDifference / _currentMissionItem.distance) * (180.0/Math.PI)))
                                                  : NaN

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
    property string _maxTelemetryText:           (isNaN(_missionMaxTelemetry) || _waypointCount === 0) ?
                                                     _noValueText :
                                                     QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(_missionMaxTelemetry).toFixed(0)
    // 순정은 값과 단위를 한 문자열로 붙였다(소수 1자리). 여기서는 요약 칸처럼 단위를 따로 두고 정수로 쓴다.
    property string _distanceText:      isNaN(_distance) ? _noValueText : QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(_distance).toFixed(0)
    property string _altDifferenceText: isNaN(_altDifference) ? _noValueText : QGroundControl.unitsConversion.metersToAppSettingsVerticalDistanceUnits(_altDifference).toFixed(0)
    property string _gradientText:      isNaN(_gradient) ? _noValueText : _gradient.toFixed(0)
    property string _azimuthText:       isNaN(_azimuth) ? _noValueText : Math.round(_azimuth) % 360
    property string _headingText:       isNaN(_heading) ? _noValueText : Math.round(_heading) % 360

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

    // 2열 x 3행 고정 격자. Flow 의 들쭉날쭉한 줄바꿈을 없앴다.
    // 폭 계산은 dFPW 배수로 하면 데스크톱(dFPW 8, 패널 240px)과 안드로이드(dFPW 11, 패널 330px)를
    // 한 번에 검증할 수 있다. 패널 폭이 dFPW*30, 좌우 여백이 dFPW 씩이라 내용 폭은 양쪽 다 28 dFPW 다.
    // 한 칸 = (28 - 1)/2 = 13.5 dFPW. 가장 긴 칸은 영문 "Max Range 12345 m" 12.9 dFPW,
    // 한국어 "최대 반경 12345 m" 11.6 dFPW 로 둘 다 들어간다(NanumGothic advance 실측).
    GridLayout {
        id:                  statGrid
        anchors.left:        parent.left
        anchors.right:       parent.right
        anchors.top:         parent.top
        anchors.leftMargin:  _margins
        anchors.rightMargin: _margins
        anchors.topMargin:   _margins
        columns:             2
        columnSpacing:       ScreenTools.defaultFontPixelWidth
        rowSpacing:          ScreenTools.defaultFontPixelHeight * 0.35

        component Stat: RowLayout {
            property string label
            property string value
            property string unit

            id:      stat
            spacing: missionStats._itemSpacing
            // 두 열을 정확히 반반으로 나눈다. 내용 폭이 열 폭을 정하면 행마다 값 위치가 어긋난다.
            Layout.fillWidth:      true
            Layout.preferredWidth: 1

            // 캡션과 강조가 한 줄에 섞인다. 기준선을 맞춰야 라벨이 숫자 위로 뜨지 않는다.
            // Row + anchors.baseline 은 레이아웃이 관리하는 자식에 쓸 수 없어 Qt.AlignBaseline 로 바꿨다.
            QGCLabel {
                Layout.alignment: Qt.AlignBaseline
                // 남는 폭은 라벨이 먹고, 칸이 모자라면 라벨만 줄어든다. 숫자는 절대 잘리지 않는다.
                Layout.fillWidth: true
                text:             stat.label
                elide:            Text.ElideRight
                // 캡션(기본x0.75)은 안드로이드에서도 잉크가 1.13mm 다. 출동 전에 실제로 읽는
                // 줄이라 본문 단으로 둔다(pointSize 를 적지 않으면 QGCLabel 기본 = 본문).
            }

            QGCLabel {
                Layout.alignment: Qt.AlignBaseline
                text:             stat.value
                font.pointSize:   missionStats._fontEmphasis
            }

            QGCLabel {
                Layout.alignment: Qt.AlignBaseline
                text:             stat.unit
                visible:          stat.unit !== ""
                font.pointSize:   missionStats._fontCaption
            }
        }

        Stat {
            // 영문 원문은 "Total Distance" 였는데 13.5 dFPW 칸에 5자리 값과 함께 들어가지 않는다.
            label: qsTr("Distance")
            value: _missionPlannedDistanceText
            unit:  QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString
        }

        Stat {
            label: qsTr("Est. Time")
            value: missionTimeText()
        }

        Stat {
            // 홈에서 가장 멀어지는 지점까지의 거리. 총 거리와 달리 링크·육안 범위 한계를 본다.
            // 순정 "Max telem dist" 와 같은 값이다.
            objectName: "missionStatsMaxTelemetry"
            label: qsTr("Max Range", "Farthest distance from home along the mission")
            value: _maxTelemetryText
            unit:  QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString
        }

        Stat {
            label: qsTr("Max Altitude")
            value: _maxRelAltitudeText
            unit:  QGroundControl.unitsConversion.appSettingsVerticalDistanceUnitsString
        }

        Stat {
            // visualItems.count - 1 은 이륙·복귀·속도변경 같은 비좌표 항목까지 센다.
            // 복합 항목(서베이 등)은 내부 웨이포인트가 몇 개든 1 로 세어진다. 그래서 "항목" 이다.
            label: qsTr("Items")
            value: _waypointCount.toString()
            unit:  qsTr("ea", "count unit, as in 5 ea")
        }

        Stat {
            // 기체가 붙어 있지 않으면 배터리 소모를 계산할 수 없다. 칸을 숨기면 격자에 구멍이 나므로
            // 값만 비운다.
            label: qsTr("Battery")
            value: _batteriesRequired >= 0 ? _batteriesRequired.toString() : _noValueText
            unit:  _batteriesRequired >= 0 ? qsTr("ea", "count unit, as in 5 ea") : ""
        }

        // 선택 항목. 요약과 같은 칸, 같은 2열이다. "이전 점 거리" 는 반 칸에 3자리 값과 함께 들어가지 않아
        // 한 줄을 다 쓴다.
        GridLayout {
            objectName:        "missionStatsSelectedItem"
            Layout.columnSpan: 2
            Layout.fillWidth:  true
            columns:           2
            columnSpacing:     statGrid.columnSpacing
            rowSpacing:        statGrid.rowSpacing
            visible:           _showSelectedItem

            QGCLabel {
                Layout.columnSpan: 2
                text:              qsTr("Selected Item")
                font.pointSize:    missionStats._fontCaption
            }

            Stat {
                objectName:        "missionStatsDistPrev"
                Layout.columnSpan: 2
                label:             qsTr("Dist prev WP")
                value:             _distanceText
                unit:              QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString
            }

            Stat {
                objectName: "missionStatsAzimuth"
                label:      qsTr("Azimuth")
                value:      _azimuthText
            }

            Stat {
                objectName: "missionStatsHeading"
                label:      qsTr("Heading")
                value:      _headingText
            }

            Stat {
                objectName: "missionStatsGradient"
                label:      qsTr("Gradient")
                value:      _gradientText
                unit:       qsTr("deg")
            }

            Stat {
                objectName: "missionStatsAltDiff"
                label:      qsTr("Alt diff")
                value:      _altDifferenceText
                unit:       QGroundControl.unitsConversion.appSettingsVerticalDistanceUnitsString
            }
        }
    }
}
