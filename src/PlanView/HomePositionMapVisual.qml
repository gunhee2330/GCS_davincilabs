import QtQuick
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

/// Home position map visual for MissionSettingsItem
MissionItemMapVisualBase {
    id: control

    indicatorComponent: homeIndicatorComponent

    Component {
        id: homeIndicatorComponent

        MapQuickItem {
            coordinate: control._missionItem.coordinate
            visible: control._missionItem.specifiesCoordinate
            z: QGroundControl.zOrderMapItems
            opacity: control.opacity
            anchorPoint.x: _homeImage.width / 2
            anchorPoint.y: _homeImage.height / 2

            sourceItem: Image {
                id: _homeImage
                source: "qrc:///qmlimages/MapHome.svg"

                property real _smallRadiusRaw: Math.ceil((ScreenTools.defaultFontPixelHeight * ScreenTools.smallFontPointRatio) / 2)
                property real _smallRadius:    _smallRadiusRaw + ((_smallRadiusRaw % 2 == 0) ? 1 : 0)

                width: _smallRadius * 2
                height: width
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit

                // 순정 MouseArea 는 안드로이드에서도 minTouchPixels 만큼 늘어나지 않는다(QGCMouseArea 만 늘어난다).
                // 집 아이콘은 14~18px 뿐이라 보이는 크기는 그대로 두고 히트 영역만 7mm(59px, 1mm = 8.49px)로 잡는다.
                QGCMouseArea {
                    anchors.centerIn: parent
                    width: Math.max(parent.width, 59, ScreenTools.defaultFontPixelHeight * 2.7)
                    height: width
                    onClicked: if (control.interactive) control.clicked(control._missionItem.sequenceNumber)
                }
            }
        }
    }
}
