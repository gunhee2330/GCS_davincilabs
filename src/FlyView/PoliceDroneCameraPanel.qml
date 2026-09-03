import QtQuick
import QtMultimedia

import QGC as App
import QGroundControl

Item {
    id: root

    property string panelTitle
    property string panelDetail
    property string streamObjectName
    property Item mirrorSource

    /// Which horizontal half of mirrorSource to show. ZT30 split-screen modes pack two
    /// sensors side by side into one stream, so a panel can present just its half.
    /// 0 = whole frame, -1 = left half, 1 = right half.
    property int mirrorHalf: 0

    /// Same convention as mirrorHalf, applied to this panel's own VideoOutput.
    property int videoCropHalf: 0

    property bool gimbalControlEnabled: true

    /// Enables long-press AI target selection on this panel.
    property bool targetPickEnabled: false
    property bool aiTargetVisible:      false
    property real aiTargetX:            0
    property real aiTargetY:            0
    property real aiTargetWidth:        0
    property real aiTargetHeight:       0
    property string aiTargetLabel:      qsTr("TARGET")
    /// Readout drawn along the bottom edge while a target is tracked: class, position, size,
    /// laser range. Empty hides it.
    property string aiTargetInfo:       ""

    readonly property alias videoSurface: videoOutput
    readonly property bool _hasDirectStream: streamObjectName.length > 0
    readonly property int _videoFillMode: VideoOutput.PreserveAspectCrop

    signal activated()

    /// Long press on the video picks an AI target. Coordinates are normalised 0..1 across
    /// the panel, which the controller scales into the stream's own resolution.
    signal targetPicked(real normalisedX, real normalisedY)

    function _sendGimbalRate() {
        if (!gimbalControlEnabled || !gimbalDrag.active) {
            return
        }

        const deadZone = Math.max(8, Math.min(width, height) * 0.035)
        const dx = Math.abs(gimbalDrag.translation.x) < deadZone ? 0 : gimbalDrag.translation.x
        const dy = Math.abs(gimbalDrag.translation.y) < deadZone ? 0 : gimbalDrag.translation.y
        const yawRate = Math.round(Math.max(-1, Math.min(1, dx / (width * 0.28))) * 100)
        const pitchRate = Math.round(Math.max(-1, Math.min(1, -dy / (height * 0.28))) * 100)
        App.SiyiCameraController.rotate(yawRate, pitchRate)
    }

    Rectangle {
        anchors.fill: parent
        color:        "#071018"
    }

    Image {
        anchors.fill: parent
        fillMode:     Image.PreserveAspectCrop
        source:       "/res/NoVideoBackground.jpg"
        opacity:      0.34
        visible:      !root._hasDirectStream && !root.mirrorSource
    }

    // Clipping frame: with videoCropHalf set, the VideoOutput is drawn at double width and
    // shifted so only the requested half of a side-by-side split stream stays visible.
    Item {
        anchors.fill: parent
        clip:         root.videoCropHalf !== 0
        visible:      root._hasDirectStream

        VideoOutput {
            id:         videoOutput
            objectName: root.streamObjectName
            fillMode:   root._videoFillMode
            width:      root.videoCropHalf === 0 ? parent.width : parent.width * 2
            height:     parent.height
            x:          root.videoCropHalf > 0 ? -parent.width : 0
            y:          0
        }
    }

    ShaderEffectSource {
        anchors.fill: parent
        sourceItem:   root.mirrorSource
        live:         true
        recursive:    true
        visible:      !!root.mirrorSource
        // A null rect means "whole item"; a half rect crops to one side of a split stream.
        sourceRect:   root.mirrorHalf === 0 || !root.mirrorSource
                          ? Qt.rect(0, 0, 0, 0)
                          : Qt.rect(root.mirrorHalf < 0 ? 0 : root.mirrorSource.width / 2,
                                    0,
                                    root.mirrorSource.width / 2,
                                    root.mirrorSource.height)
    }

    PoliceDroneTargetOverlay {
        anchors.fill:   parent
        targetVisible: root.aiTargetVisible
        targetX:       root.aiTargetX
        targetY:       root.aiTargetY
        targetWidth:   root.aiTargetWidth
        targetHeight:  root.aiTargetHeight
        targetLabel:   root.aiTargetLabel
        fillMode:      Image.PreserveAspectCrop
    }

    Rectangle {
        anchors.left:   parent.left
        anchors.top:    parent.top
        anchors.margins: 8
        width:          panelHeading.implicitWidth + 18
        height:         panelHeading.implicitHeight + 10
        radius:         3
        color:          "#c0121b24"

        Text {
            id:             panelHeading
            anchors.centerIn: parent
            color:          "white"
            font.bold:      true
            font.pixelSize: Math.max(12, Screen.pixelDensity * 3.2)
            text:           root.panelTitle
        }
    }

    Rectangle {
        anchors.right:   parent.right
        anchors.top:     parent.top
        anchors.margins: 8
        width:           detailText.implicitWidth + 16
        height:          detailText.implicitHeight + 8
        radius:          3
        color:           "#b0121b24"
        visible:         root.panelDetail.length > 0

        Text {
            id:             detailText
            anchors.centerIn: parent
            color:          "#c6d6e3"
            font.pixelSize: Math.max(10, Screen.pixelDensity * 2.7)
            text:           root.panelDetail
        }
    }

    Rectangle {
        anchors.left:    parent.left
        anchors.bottom:  parent.bottom
        anchors.margins: 8
        width:           Math.min(parent.width - 16, targetInfoText.implicitWidth + 16)
        height:          targetInfoText.implicitHeight + 8
        radius:          3
        color:           "#c0121b24"
        visible:         root.aiTargetInfo.length > 0

        Text {
            id:               targetInfoText
            anchors.centerIn: parent
            width:            parent.width - 16
            color:            "#ffd166"
            font.bold:        true
            font.pixelSize:   Math.max(11, Screen.pixelDensity * 2.9)
            elide:            Text.ElideRight
            text:             root.aiTargetInfo
        }
    }

    Rectangle {
        anchors.fill: parent
        color:        "transparent"
        border.color: gimbalDrag.active ? "#33c7ff" : "#5f7180"
        border.width: gimbalDrag.active ? 3 : 1
    }

    Rectangle {
        x:       gimbalDrag.centroid.position.x - width / 2
        y:       gimbalDrag.centroid.position.y - height / 2
        width:   Math.max(42, Screen.pixelDensity * 10)
        height:  width
        radius:  width / 2
        color:   "#4033c7ff"
        border.color: "#33c7ff"
        visible: gimbalDrag.active
    }

    // Long press selects an AI target. A short tap still toggles fullscreen, and the drag
    // threshold keeps a gimbal slew from being mistaken for a selection.
    TapHandler {
        id:                 targetPick
        enabled:            root.targetPickEnabled
        longPressThreshold: 0.6
        gesturePolicy:      TapHandler.DragThreshold
        onLongPressed: {
            const p = point.position
            root.targetPicked(p.x / Math.max(1, root.width), p.y / Math.max(1, root.height))
            pickFlash.x = p.x - pickFlash.width / 2
            pickFlash.y = p.y - pickFlash.height / 2
            pickFlash.flash()
        }
    }

    Rectangle {
        id:      pickFlash
        width:   Math.max(48, Screen.pixelDensity * 11)
        height:  width
        radius:  width / 2
        color:   "transparent"
        border.color: "#ff3b30"
        border.width: 3
        opacity: 0

        function flash() { flashAnim.restart() }

        NumberAnimation {
            id:       flashAnim
            target:   pickFlash
            property: "opacity"
            from:     1
            to:       0
            duration: 700
        }
    }

    Timer {
        id:       gimbalRateTimer
        interval: 80
        repeat:   true
        onTriggered: root._sendGimbalRate()
    }

    DragHandler {
        id:                  gimbalDrag
        target:              null
        enabled:             root.gimbalControlEnabled
        dragThreshold:       Math.max(10, Screen.pixelDensity * 2)
        grabPermissions:     PointerHandler.CanTakeOverFromItems | PointerHandler.ApprovesTakeOverByAnything

        onActiveChanged: {
            if (active) {
                root._sendGimbalRate()
                gimbalRateTimer.start()
            } else {
                gimbalRateTimer.stop()
                App.SiyiCameraController.stopRotation()
            }
        }

        onTranslationChanged: root._sendGimbalRate()
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy:   TapHandler.DragThreshold
        onTapped:        root.activated()
    }

    Component.onDestruction: App.SiyiCameraController.stopRotation()
}
