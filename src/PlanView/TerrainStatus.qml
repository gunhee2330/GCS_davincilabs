import QtQuick
import QtGraphs

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:         root
    radius:     ScreenTools.defaultFontPixelWidth * 0.5
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
    readonly property real _fontCaption:    ScreenTools.smallFontPointSize

    QGCPalette { id: qgcPal }

    // 세로로 돌린 제목은 폭 한 줄과 밴드 높이를 함께 먹는다. 7인치에서는 둘 다 아깝다.
    // 축 눈금이 이미 숫자를 보여주므로 기준(해발)만 가로로 짧게 적는다.
    QGCLabel {
        id:                  titleLabel
        // 아래 QGCFlickable 보다 먼저 선언돼 있어 그대로 두면 차트 배경에 덮인다.
        z:                   1
        anchors.top:         parent.top
        anchors.left:        parent.left
        anchors.leftMargin:  _margins
        anchors.topMargin:   1
        font.pointSize:      root._fontCaption
        color:              qgcPal.text
        opacity:            0.7
        text:               qsTr("AMSL %1").arg(_unitsConversion.appSettingsVerticalDistanceUnitsString)
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
                // 위쪽 여백은 눈금 잘림 방지 겸 '해발' 제목 자리다.
                marginTop:          ScreenTools.defaultFontPixelHeight * 1.1
                marginRight:        ScreenTools.defaultFontPixelWidth * 2   // Prevents clipping last tick mark
                marginBottom:       -ScreenTools.defaultFontPixelHeight / 2 // For some reason you can't get rid of bottom margin by setting to 0
                marginLeft:         0

                theme: GraphsTheme {
                    colorScheme:                qgcPal.globalTheme === QGCPalette.Light ? GraphsTheme.ColorScheme.Light : GraphsTheme.ColorScheme.Dark
                    backgroundColor:            "transparent"
                    backgroundVisible:          false
                    plotAreaBackgroundColor:     qgcPal.window
                    grid.mainColor:             applyOpacity(qgcPal.text, 0.18)
                    grid.subColor:              applyOpacity(qgcPal.text, 0.10)
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
                    labelDecimals:              1
                }

                axisY: ValueAxis {
                    id:                         axisY
                    min:                        _unitsConversion.metersToAppSettingsVerticalDistanceUnits(_minAMSLAltitude)
                    max:                        _unitsConversion.metersToAppSettingsVerticalDistanceUnits(_maxAMSLAltitude)
                    lineVisible:                true
                    // 64px 안에 눈금 4개를 넣으면 라벨끼리 겹친다. 최저·중간·최고 세 개면 읽힌다.
                    tickInterval:               (max - min) > 0 ? (max - min) / 2 : 1
                    labelDecimals:              1
                }

                // The order of the LineSeries is important to work around nasty bugs in QtGraphs where series just don't display. If you put
                // terrain and flight first you end up with cases where flight doesn't display no matter what other sorts of workarounds you try.
                // Putting missing and collision first seems to prevent the problem.

                LineSeries {
                    id:         missingSeries
                    color:      "yellow"
                    width:      2
                }

                LineSeries {
                    id:         collisionSeries
                    color:      "red"
                    width:      flightSeries.width * 3
                }

                LineSeries {
                    id:         terrainSeries
                    color:      "green"
                    width:      2
                }

                LineSeries {
                    id:         flightSeries
                    color:      "orange"
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
