import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

Canvas {
    id:     root

    width:  _width
    height: _height

    signal clicked(point position)

    property string label                           ///< Label to show to the side of the index indicator
    property int    index:                  0       ///< Index to show in the indicator, 0 will show single char label instead, -1 first char of label in indicator full label to the side
    property bool   checked:                false
    property bool   small:                  !checked
    property bool   child:                  false
    property bool   highlightSelected:      false
    property var    color:                  checked ? "green" : (child ? qgcPal.mapIndicatorChild : qgcPal.mapIndicator)
    property real   anchorPointX:           _height / 2
    property real   anchorPointY:           _height / 2
    property bool   specifiesCoordinate:    true
    property real   gimbalYaw
    property real   vehicleYaw
    property bool   showGimbalYaw:          false
    property bool   showSequenceNumbers:    true

    // 히트 영역의 최소 크기(px). 보이는 크기와 분리되어 있다 — 지도 위 요소라 마커를 키우면 지도가 가려진다.
    // 야외 손가락 기준 7mm. 7인치 1280x800(1mm = 8.49px)에서 59px 이고, UI 배율이 올라가면
    // 마커도 커지므로 dFPH 배수도 함께 본다(안드로이드 14pt: dFPH 22 → 59.4px).
    // 목록 배지(MissionItemEditor)와 고도 프로파일 배지(TerrainStatus)는 서로 바짝 붙어 있어
    // 같이 넓히면 이웃 배지를 삼킨다. 그래서 지도에서 단독으로 눌리는 마커에서만 켠다 —
    // 그 세 곳이 highlightSelected 를 켜므로(MissionItemIndicator.qml:29,
    // TakeoffItemMapVisual.qml:122, RallyPointMapVisuals.qml:63) 그것을 기본값으로 쓴다.
    // 지도 밖에서 highlightSelected 를 켜게 되면 그 인스턴스에서 touchTargetSize: 0 으로 끊는다.
    property real   touchTargetSize:        highlightSelected ? Math.max(59, ScreenTools.defaultFontPixelHeight * 2.7) : 0

    property real   _width:             showGimbalYaw ? Math.max(_gimbalYawWidth, labelControl.visible ? labelControl.width : indicator.width) : (labelControl.visible ? labelControl.width : indicator.width)
    property real   _height:            showGimbalYaw ? _gimbalYawWidth : (labelControl.visible ? labelControl.height : indicator.height)
    property real   _gimbalYawRadius:   ScreenTools.defaultFontPixelHeight
    property real   _gimbalYawWidth:    _gimbalYawRadius * 2
    property real   _smallRadiusRaw:    Math.ceil((ScreenTools.defaultFontPixelHeight * ScreenTools.smallFontPointRatio) / 2)
    property real   _smallRadius:       _smallRadiusRaw + ((_smallRadiusRaw % 2 == 0) ? 1 : 0) // odd number for better centering
    property real   _normalRadiusRaw:   Math.ceil(ScreenTools.defaultFontPixelHeight * 0.66)
    property real   _normalRadius:      _normalRadiusRaw + ((_normalRadiusRaw % 2 == 0) ? 1 : 0)
    property real   _indicatorRadius:   small ? _smallRadius : _normalRadius
    property real   _gimbalRadians:     degreesToRadians(vehicleYaw + gimbalYaw - 90)
    property real   _labelMargin:       2
    property real   _labelRadius:       _indicatorRadius + _labelMargin
    property string _label:             label.length > 1 ? label : ""
    property string _index:             index === 0 || index === -1 ? label.charAt(0) : (showSequenceNumbers ? index : "")

    onColorChanged:         requestPaint()
    onShowGimbalYawChanged: requestPaint()
    onGimbalYawChanged:     requestPaint()
    onVehicleYawChanged:    requestPaint()

    QGCPalette { id: qgcPal }

    function degreesToRadians(degrees) {
        return (Math.PI/180)*degrees
    }

    function paintGimbalYaw(context) {
        if (showGimbalYaw) {
            context.save()
            context.globalAlpha = 0.75
            context.beginPath()
            context.moveTo(anchorPointX, anchorPointY)
            context.arc(anchorPointX, anchorPointY, _gimbalYawRadius,  _gimbalRadians + degreesToRadians(45), _gimbalRadians + degreesToRadians(-45), true /* clockwise */)
            context.closePath()
            context.fillStyle = "white"
            context.fill()
            context.restore()
        }
    }

    onPaint: {
        var context = getContext("2d")
        context.clearRect(0, 0, width, height)
        paintGimbalYaw(context)
    }

    Rectangle {
        id:                     labelControl
        anchors.leftMargin:     -((_labelMargin * 2) + indicator.width)
        anchors.rightMargin:    -(_labelMargin * 2)
        anchors.fill:           labelControlLabel
        color:                  "white"
        opacity:                0.5
        radius:                 _labelRadius
        visible:                _label.length !== 0 && !small
    }

    QGCLabel {
        id:                     labelControlLabel
        anchors.topMargin:      -_labelMargin
        anchors.bottomMargin:   -_labelMargin
        anchors.leftMargin:     _labelMargin
        anchors.left:           indicator.right
        anchors.top:            indicator.top
        anchors.bottom:         indicator.bottom
        color:                  "black"
        text:                   _label
        verticalAlignment:      Text.AlignVCenter
        visible:                labelControl.visible
    }

    Rectangle {
        id:                             indicator
        anchors.horizontalCenter:       parent.left
        anchors.verticalCenter:         parent.top
        anchors.horizontalCenterOffset: anchorPointX
        anchors.verticalCenterOffset:   anchorPointY
        width:                          _indicatorRadius * 2
        height:                         width
        color:                          root.color
        radius:                         _indicatorRadius

        QGCLabel {
            anchors.fill:           parent
            horizontalAlignment:    Text.AlignHCenter
            verticalAlignment:      Text.AlignVCenter
            color:                  "white"
            font.pointSize:         ScreenTools.defaultFontPointSize
            fontSizeMode:           Text.Fit
            text:                   _index
        }
    }

    // Extra circle to indicate selection
    Rectangle {
        width:          indicator.width * 2
        height:         width
        radius:         width * 0.5
        color:          Qt.rgba(0,0,0,0)
        border.color:   Qt.rgba(1,1,1,0.5)
        border.width:   1
        visible:        checked && highlightSelected
        anchors.centerIn: indicator
    }

    // The mouse click area is always at least the size of a normal indicator
    Item {
        id:                 mouseAreaFill
        width:              Math.max(_normalRadius * 2, touchTargetSize)
        height:             width
        anchors.centerIn:   indicator
    }

    QGCMouseArea {
        fillItem:   mouseAreaFill
        onClicked: (mouse) => {
            focus = true
            parent.clicked(Qt.point(mouse.x, mouse.y))
        }
    }
}
