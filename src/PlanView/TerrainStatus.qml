import QtQuick
import QtGraphs

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:         root
    // 둥근 모서리는 소비자 앱 인상을 준다. 계기판처럼 각지게 둔다.
    radius:     0
    // 왼쪽 모서리는 접기 버튼과 맞닿는다. 둥글리면 이음매에 지도가 비치는 홈이 생겨 밴드가 두 조각으로 보인다.
    topLeftRadius:      0
    bottomLeftRadius:   0
    // opacity 를 루트에 걸면 축 눈금·제목 글자까지 흐려져 위 숫자줄과 밝기가 어긋난다.
    // 배경만 반투명하게 하고 글자는 온전한 밝기로 둔다.
    color:      Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.80)
    clip:       true

    property var missionController

    signal setCurrentSeqNum(int seqNum)

    property real _margins:                 ScreenTools.defaultFontPixelWidth / 2
    property var  _visualItems:             missionController.visualItems
    property real _altRange:                _maxAMSLAltitude - _minAMSLAltitude
    property real _indicatorSpacing:        5
    property real _minAMSLAltitude:         isNaN(terrainProfile.minAMSLAlt) ? 0 : terrainProfile.minAMSLAlt
    property real _maxAMSLAltitude:         isNaN(terrainProfile.maxAMSLAlt) ? 100 : terrainProfile.maxAMSLAlt
    property real _missionTotalDistance:    isNaN(missionController.missionTotalDistance) ? 100 : missionController.missionTotalDistance
    property var  _unitsConversion:         QGroundControl.unitsConversion

    // 글꼴 정책은 MissionStats.qml 상단 주석 참조. 4파일 공통, 나중에 한 곳으로 모을 것.
    readonly property real _fontEmphasis:   ScreenTools.defaultFontPointSize * 1.15
    readonly property real _fontCaption:    ScreenTools.smallFontPointSize

    QGCPalette { id: qgcPal }

    // 프로파일 4선의 야외 대비. 리터럴 "yellow"/"orange" 는 Light 배경(#ffffff)에서 1.07:1,
    // 1.97:1 이라 '지형 고도 모름' 과 '계획 고도' 가 눈부심 대책으로 팔레트를 바꾼 순간 사라진다.
    // 팔레트의 colorYellow/colorOrange 도 Light 에서 2.73:1 / 3.61:1 이라 비텍스트 기준 3:1 을
    // 겨우 넘거나 못 넘는다. 한 색으로 두 테마 4.5:1 을 동시에 만족시키는 것은 불가능하므로
    // (Light 는 상대휘도 ≤0.183, Dark 는 ≥0.247 을 요구한다) 테마별로 고른다.
    // 주석의 수치는 qgcPal.window(Light #ffffff / Dark #222222) 대비 WCAG 대비비다.
    readonly property bool  _lightTheme:      qgcPal.globalTheme === QGCPalette.Light
    readonly property color _colorMissing:    _lightTheme ? "#757500" : "#ffff00"  //  4.88:1 / 14.82:1
    readonly property color _colorCollision:  _lightTheme ? "#b52b2b" : "#ff4d4d"  //  6.28:1 /  4.86:1
    readonly property color _colorTerrain:    _lightTheme ? "#007a28" : "#00e04b"  //  5.51:1 /  8.91:1
    readonly property color _colorFlight:     _lightTheme ? "#a34f00" : "#de8500"  //  5.71:1 /  5.65:1

    // 세로로 돌린 제목은 폭 한 줄과 밴드 높이를 함께 먹는다. 7인치에서는 둘 다 아깝다.
    // 축 눈금이 이미 숫자를 보여주므로 기준(해발)만 가로로 짧게 적는다.
    QGCLabel {
        id:                  titleLabel
        // 아래 QGCFlickable 보다 먼저 선언돼 있어 그대로 두면 차트 배경에 덮인다.
        z:                   1
        // 아래로 내리면 X축 첫 눈금과 겹친다. 차트 위쪽 여백에 앉힌다.
        // 왼쪽은 Y축 최상단 눈금이 위 여백까지 올라와 쓰는 자리라 오른쪽 끝에 붙인다.
        anchors.top:         parent.top
        anchors.right:       parent.right
        anchors.rightMargin: _margins
        anchors.topMargin:   1
        // 야외 판독 기준. 캡션(기본×0.75) + opacity 0.7 은 잉크 높이 1.1mm 로 7인치에서 읽히지 않는다.
        font.pointSize:      root._fontEmphasis
        color:              qgcPal.text
        text:               qsTr("AMSL (%1)").arg(_unitsConversion.appSettingsVerticalDistanceUnitsString)
    }

    QGCFlickable {
        id:                 terrainProfileFlickable
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.leftMargin: 0
        anchors.left:       parent.left
        anchors.right:      parent.right
        clip:               true

        Item {
            height: terrainProfileFlickable.height
            width:  terrainProfileFlickable.width

            GraphsView {
                id:                 chart
                anchors.fill:       parent
                // 위쪽 여백은 눈금 잘림 방지 겸 '해발' 라벨 자리다. 라벨이 기본×1.15 로 커져 1.1 로는 모자란다.
                marginTop:          ScreenTools.defaultFontPixelHeight * 1.3
                marginRight:        ScreenTools.defaultFontPixelWidth * 2   // Prevents clipping last tick mark
                marginBottom:       -ScreenTools.defaultFontPixelHeight / 2 // For some reason you can't get rid of bottom margin by setting to 0
                marginLeft:         0

                theme: GraphsTheme {
                    colorScheme:                qgcPal.globalTheme === QGCPalette.Light ? GraphsTheme.ColorScheme.Light : GraphsTheme.ColorScheme.Dark
                    backgroundColor:            "transparent"
                    backgroundVisible:          false
                    // 아래 선·격자 대비 수치는 전부 이 배경이 불투명하다는 전제로 계산했다.
                    // 기본값에 기대지 않고 명시한다.
                    plotAreaBackgroundVisible:   true
                    plotAreaBackgroundColor:     qgcPal.window
                    // 0.18/0.10 은 두 테마 모두 1.19~1.80:1 이라 64px 밴드에서 사라진다.
                    // 격자가 고도 눈금의 유일한 기준선이므로 비텍스트 기준 3:1 을 넘긴다.
                    grid.mainColor:             applyOpacity(qgcPal.text, 0.60)  // 3.69:1 / 6.61:1
                    grid.subColor:              applyOpacity(qgcPal.text, 0.53)  // 3.05:1 / 5.38:1
                    grid.mainWidth:             1
                    labelBackgroundVisible:     false
                    labelTextColor:             qgcPal.text
                    // GraphsTheme 는 QGCLabel 이 아니라 글꼴을 물려받지 못한다. 축 눈금 숫자가
                    // 아래 숫자 밴드와 같아 보이도록 본문 글꼴을 그대로 지정한다.
                    axisXLabelFont.family:      ScreenTools.normalFontFamily
                    axisXLabelFont.pointSize:   root._fontCaption
                    axisYLabelFont.family:      ScreenTools.normalFontFamily
                    axisYLabelFont.pointSize:   root._fontCaption
                }

                axisX: ValueAxis {
                    id:                         axisX
                    min:                        0
                    max:                        _unitsConversion.metersToAppSettingsHorizontalDistanceUnits(_missionTotalDistance)
                    lineVisible:                true
                    tickInterval:               max > 0 ? max / 4 : 1
                    labelDecimals:              0
                }

                axisY: ValueAxis {
                    id:                         axisY
                    min:                        _unitsConversion.metersToAppSettingsVerticalDistanceUnits(_minAMSLAltitude)
                    max:                        _unitsConversion.metersToAppSettingsVerticalDistanceUnits(_maxAMSLAltitude)
                    lineVisible:                true
                    // 64px 짜리 프로파일에 눈금 라벨 3개는 서로 겹친다. 최저·최고 두 개만 남긴다.
                    tickInterval:               (max - min) > 0 ? (max - min) : 1
                    labelDecimals:              0
                }

                // The order of the LineSeries is important to work around nasty bugs in QtGraphs where series just don't display. If you put
                // terrain and flight first you end up with cases where flight doesn't display no matter what other sorts of workarounds you try.
                // Putting missing and collision first seems to prevent the problem.

                LineSeries {
                    id:         missingSeries
                    color:      root._colorMissing
                    width:      2
                }

                LineSeries {
                    id:         collisionSeries
                    color:      root._colorCollision
                    width:      flightSeries.width * 3
                }

                LineSeries {
                    id:         terrainSeries
                    color:      root._colorTerrain
                    // 지형선과 비행선은 배경 대비는 각각 통과하지만 서로의 휘도가 거의 같아
                    // (Light 1.04:1) 색만으로는 구분되지 않고 적록색약에서는 완전히 무너진다.
                    // 굵기를 색과 독립된 두 번째 구분 채널로 쓴다 — 충돌선이 쓰는 방식과 같다.
                    width:      4
                }

                LineSeries {
                    id:         flightSeries
                    color:      root._colorFlight
                    width:      2
                }
            }

            TerrainProfile {
                id:                 terrainProfile
                x:                  chart.plotArea.x
                y:                  chart.plotArea.y
                height:             chart.plotArea.height
                visibleWidth:       chart.plotArea.width
                missionController:  root.missionController
                horizontalScale:    _unitsConversion.metersToAppSettingsHorizontalDistanceUnits(1)
                verticalScale:      _unitsConversion.metersToAppSettingsVerticalDistanceUnits(1)

                onProfileChanged:   terrainProfile.updateSeries(terrainSeries, flightSeries, missingSeries, collisionSeries)

                Repeater {
                    model: missionController.visualItems

                    Item {
                        id:             topLevelItem
                        anchors.fill:   parent
                        visible:        object.specifiesCoordinate && !object.standaloneCoordinate

                        Rectangle {
                            id:         simpleItem
                            height:     terrainProfile.height
                            width:      1
                            color:      qgcPal.text
                            x:          (object.distanceFromStart * terrainProfile.pixelsPerMeter)
                            visible:    object.isSimpleItem || object.isSingleItem

                            MissionItemIndexLabel {
                                anchors.horizontalCenter:   parent.horizontalCenter
                                anchors.bottom:             parent.bottom
                                small:                      true
                                checked:                    object.isCurrentItem
                                label:                      object.abbreviation.charAt(0)
                                index:                      object.abbreviation.charAt(0) > 'A' && object.abbreviation.charAt(0) < 'z' ? -1 : object.sequenceNumber
                                onClicked:                  root.setCurrentSeqNum(object.sequenceNumber)
                            }
                        }

                        Rectangle {
                            id:         complexItemEntry
                            height:     terrainProfile.height
                            width:      1
                            color:      qgcPal.text
                            x:          (object.distanceFromStart * terrainProfile.pixelsPerMeter)
                            visible:    complexItem.visible

                            MissionItemIndexLabel {
                                anchors.horizontalCenter:   parent.horizontalCenter
                                anchors.bottom:             parent.bottom
                                small:                      true
                                checked:                    object.isCurrentItem
                                index:                      object.sequenceNumber
                                onClicked:                  root.setCurrentSeqNum(object.sequenceNumber)
                            }
                        }

                        Rectangle {
                            id:         complexItemExit
                            height:     terrainProfile.height
                            width:      1
                            color:      qgcPal.text
                            x:          ((object.distanceFromStart + object.complexDistance) * terrainProfile.pixelsPerMeter)
                            visible:    complexItem.visible

                            MissionItemIndexLabel {
                                anchors.horizontalCenter:   parent.horizontalCenter
                                anchors.bottom:             parent.bottom
                                small:                      true
                                checked:                    object.isCurrentItem
                                index:                      object.lastSequenceNumber
                                onClicked:                  root.setCurrentSeqNum(object.sequenceNumber)
                            }
                        }

                        Rectangle {
                            id:             complexItem
                            anchors.bottom: parent.bottom
                            x:              (object.distanceFromStart * terrainProfile.pixelsPerMeter)
                            width:          complexItem.visible ? object.complexDistance * terrainProfile.pixelsPerMeter : 0
                            height:         patternNameLabel.height
                            color:          "green"
                            opacity:        0.5
                            visible:        !object.isSimpleItem && !object.isSingleItem

                            QGCMouseArea {
                                anchors.fill:   parent
                                onClicked:      root.setCurrentSeqNum(object.sequenceNumber)
                            }

                            QGCLabel {
                                id:                         patternNameLabel
                                anchors.horizontalCenter:   parent.horizontalCenter
                                text:                       complexItem.visible ? object.patternName : ""
                            }
                        }
                    }
                }
            }
        }
    }

    function applyOpacity(colorIn, opacity){
        return Qt.rgba(colorIn.r, colorIn.g, colorIn.b, opacity)
    }
}
