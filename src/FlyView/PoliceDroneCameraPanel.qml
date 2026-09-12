import QtQuick
import QtMultimedia

import QGC as App
import QGroundControl
import QGroundControl.Controls

Item {
    id: root

    property string panelTitle
    property string panelDetail
    /// Windowed panels sit under a title bar that already names them, so the chips only come
    /// out full screen, where there is no bar and the operator needs to know what they are on.
    property bool   showChrome: true
    property string streamObjectName

    property bool gimbalControlEnabled: true

    /// Draws the on-device detector's face mosaics, and lets a long press snap to a person box.
    /// Only the stream the detector taps (videoContent) has results to draw.
    property bool personDetectionEnabled: false

    /// Draws the proximity ring around the centre of the picture. Only for a camera bolted to
    /// the airframe: the ring is vehicle-relative, so on a gimballed window its arcs would point
    /// wherever the pod happens to be looking.
    property bool proximityRingEnabled: false

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

    /// Whether the module is following a target, drawn as a chip in the corner opposite the
    /// window's name. Null on the panels that have nothing to say about tracking, which is
    /// every one but the module's own picture.
    property var trackingActive: null

    /// Whether the aircraft is flying after that target - the gimbal's own follow, a separate
    /// command on a separate link. Null hides it, as above.
    property var followActive: null

    /// Decoded frame size of this panel's stream, empty until frames arrive. The AI module
    /// scales target selections by the stream resolution, which only this panel can know.
    readonly property rect streamRect: videoOutput.sourceRect

    readonly property bool _hasDirectStream: streamObjectName.length > 0
    readonly property int _videoFillMode: VideoOutput.PreserveAspectCrop

    signal activated()

    /// Long press on the video picks an AI target. Coordinates are normalised 0..1 across
    /// the video frame, which the controller scales into the stream's own resolution.
    signal targetPicked(real normalisedX, real normalisedY)

    /// Long press on a box the on-device detector drew hands that box to the AI module instead
    /// of the bare point, so the tracker starts on the whole object. Same 0..1 frame coordinates.
    signal targetBoxPicked(real left, real top, real right, real bottom)

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
        visible:      !root._hasDirectStream
    }

    VideoOutput {
        id:           videoOutput
        anchors.fill: parent
        objectName:   root.streamObjectName
        fillMode:     root._videoFillMode
        visible:      root._hasDirectStream
    }

    // No trackedRect any more: it used to suppress our green box under the pod's tracked target
    // so one person did not read as two. With only mosaics left, suppressing there would have
    // meant the one face the operator is actively following was the one left uncovered.
    PoliceDronePersonOverlay {
        id:           personOverlay
        anchors.fill: parent
        videoOutput:  videoOutput
        enabled:      root.personDetectionEnabled && root._hasDirectStream
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

    // Centred on the panel rather than on contentRect: the fill mode crops rather than
    // letterboxes, so the picture's centre is the panel's centre even before frames arrive.
    PoliceDroneProximityRing {
        anchors.centerIn:   parent
        active:             root.proximityRingEnabled
        ringRadius:         Math.min(root.width, root.height) * 0.33
        showDistanceLabels: true
    }

    Rectangle {
        anchors.left:   parent.left
        anchors.top:    parent.top
        anchors.margins: 8
        width:          panelHeading.implicitWidth + 18
        height:         panelHeading.implicitHeight + 10
        radius:         3
        color:          "#c0121b24"
        visible:        root.showChrome

        Text {
            id:             panelHeading
            anchors.centerIn: parent
            color:          "white"
            font.bold:      true
            font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.7
            text:           root.panelTitle
        }
    }

    /// A dot and a word for one piece of state. Grey when it is not happening - idle is not a
    /// fault, and a red resting state reads as one across a whole flight.
    component StateChip: Rectangle {
        property bool   lit
        property color  litColor
        property string label

        width:  chipRow.implicitWidth + 16
        height: chipRow.implicitHeight + 8
        radius: 3
        color:  "#c0121b24"

        Row {
            id:               chipRow
            anchors.centerIn: parent
            spacing:          6

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width:                  Math.max(9, ScreenTools.defaultFontPixelHeight * 0.42)
                height:                 width
                radius:                 width / 2
                color:                  lit ? litColor : "#9aa3ab"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color:                  lit ? litColor : "#9aa3ab"
                font.bold:              true
                font.pixelSize:         Math.max(11, ScreenTools.defaultFontPixelHeight * 0.62)
                text:                   label
            }
        }
    }

    // Tracking and follow sit on the picture they are happening on rather than in the detection
    // card across the screen: the operator watching a target run is looking here. Drawn windowed
    // as well as full screen - this is state, not chrome naming the window.
    Row {
        id:              stateChips
        anchors.right:   parent.right
        anchors.top:     parent.top
        anchors.margins: 8
        spacing:         6
        visible:         (root.followActive !== null) || (root.trackingActive !== null)

        // Follow leads: it is the one that moves the airframe.
        StateChip {
            visible:  root.followActive !== null
            lit:      root.followActive === true
            litColor: "#1f9fd0"
            label:    lit ? qsTr("추종 중") : qsTr("추종 대기")
        }

        StateChip {
            visible:  root.trackingActive !== null
            lit:      root.trackingActive === true
            litColor: "#42d66b"
            label:    lit ? qsTr("추적 중") : qsTr("추적 대기")
        }
    }

    Rectangle {
        anchors.right:   parent.right
        // Under the state chips when both are up, so neither has to be read through the other.
        anchors.top:     stateChips.visible ? stateChips.bottom : parent.top
        anchors.margins: 8
        width:           detailText.implicitWidth + 16
        height:          detailText.implicitHeight + 8
        radius:          3
        color:           "#b0121b24"
        visible:         root.showChrome && root.panelDetail.length > 0

        Text {
            id:             detailText
            anchors.centerIn: parent
            color:          "#c6d6e3"
            font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.6
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
            font.pixelSize:   ScreenTools.defaultFontPixelHeight * 0.64
            elide:            Text.ElideRight
            text:             root.aiTargetInfo
        }
    }

    // Only while the gimbal is being dragged. No resting border - it was the grey hairline
    // boxing every camera in.
    Rectangle {
        anchors.fill: parent
        color:        "transparent"
        visible:      gimbalDrag.active
        border.color: "#33c7ff"
        border.width: 3
    }

    Rectangle {
        x:       gimbalDrag.centroid.position.x - width / 2
        y:       gimbalDrag.centroid.position.y - height / 2
        width:   ScreenTools.minTouchPixels * 1.2
        height:  width
        radius:  width / 2
        color:   "#4033c7ff"
        border.color: "#33c7ff"
        visible: gimbalDrag.active
    }

    // Long press proposes an AI target; the slide below commits it. A short tap still toggles
    // fullscreen, and the drag threshold keeps a gimbal slew from being mistaken for a selection.
    TapHandler {
        id:                 targetPick
        enabled:            root.targetPickEnabled
        longPressThreshold: 0.6
        gesturePolicy:      TapHandler.DragThreshold
        onLongPressed: {
            const p = point.position
            root._proposeTarget(p)
            pickFlash.x = p.x - pickFlash.width / 2
            pickFlash.y = p.y - pickFlash.height / 2
            pickFlash.flash()
        }
    }

    /// The pick the operator has proposed and not yet confirmed: a detected box when the press
    /// landed on one, otherwise the point itself. Null when nothing is pending.
    property var _pendingBox:   null
    property var _pendingPoint: null

    function _proposeTarget(p) {
        const box = personOverlay.boxAt(p.x, p.y)
        root._pendingBox = box
        root._pendingPoint = box ? null : Qt.point(p.x, p.y)
        pendingTimeout.restart()
    }

    function _clearProposal() {
        pendingTimeout.stop()
        root._pendingBox = null
        root._pendingPoint = null
    }

    function _commitProposal() {
        if (root._pendingBox) {
            const b = root._pendingBox
            root.targetBoxPicked(b.x, b.y, b.x + b.width, b.y + b.height)
        } else if (root._pendingPoint) {
            // Through contentRect, not the panel size: fullscreen on a non-16:9 screen crops
            // the frame, and the module wants frame coordinates.
            const c = videoOutput.contentRect
            const p = root._pendingPoint
            root.targetPicked((p.x - c.x) / Math.max(1, c.width), (p.y - c.y) / Math.max(1, c.height))
        }
        root._clearProposal()
    }

    // A proposal the operator walks away from must not sit there waiting to be slid by accident.
    Timer {
        id:          pendingTimeout
        interval:    12000
        onTriggered: root._clearProposal()
    }

    // The picked box, marked in the colour it will keep once the pod is following it.
    Rectangle {
        readonly property rect _content: videoOutput.contentRect
        visible:      root._pendingBox !== null
        x:            _content.x + (root._pendingBox ? root._pendingBox.x * _content.width : 0)
        y:            _content.y + (root._pendingBox ? root._pendingBox.y * _content.height : 0)
        width:        root._pendingBox ? root._pendingBox.width * _content.width : 0
        height:       root._pendingBox ? root._pendingBox.height * _content.height : 0
        color:        "transparent"
        border.color: "#ff9500"
        border.width: Math.max(2, ScreenTools.defaultFontPixelWidth * 0.25)

        SequentialAnimation on opacity {
            running: parent.visible
            loops:   Animation.Infinite
            NumberAnimation { to: 0.35; duration: 500 }
            NumberAnimation { to: 1.0;  duration: 500 }
        }
    }

    // Same gesture as takeoff: a deliberate slide, because this one starts pointing the pod.
    //
    // Left corner and always on screen rather than appearing under the finger once a press has
    // landed. Centred it sat on the detection card, which is the other thing the bottom edge
    // carries full screen, and an operator who has never held a press down has no way to learn
    // the control exists. Showing it idle costs one strip of picture and teaches the gesture:
    // the slider reads as unavailable until a press proposes a target, which is the state it is
    // actually in.
    Row {
        anchors.left:         parent.left
        anchors.bottom:       parent.bottom
        anchors.leftMargin:   ScreenTools.defaultFontPixelWidth
        anchors.bottomMargin: ScreenTools.defaultFontPixelHeight
        spacing:              ScreenTools.defaultFontPixelWidth
        visible:              root.targetPickEnabled

        readonly property bool _pending: root._pendingBox !== null || root._pendingPoint !== null

        SliderSwitch {
            anchors.verticalCenter: parent.verticalCenter
            width:                  Math.min(implicitWidth * 1.2, root.width * 0.6)
            enabled:                parent._pending
            opacity:                enabled ? 1 : 0.45
            confirmText:            enabled ? qsTr("이 대상을 추적") : qsTr("화면을 길게 눌러 대상 선택")
            onAccept:               root._commitProposal()
        }

        QGCButton {
            anchors.verticalCenter: parent.verticalCenter
            text:                   qsTr("취소")
            visible:                parent._pending
            onClicked:              root._clearProposal()
        }
    }

    Rectangle {
        id:      pickFlash
        width:   ScreenTools.minTouchPixels * 1.4
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
        dragThreshold:       ScreenTools.minTouchPixels * 0.4
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
        // Same threshold as the target pick: a hold that selected a target must not also
        // count as a tap on release and flip fullscreen.
        longPressThreshold: targetPick.longPressThreshold
        onTapped:        root.activated()
    }

    Component.onDestruction: App.SiyiCameraController.stopRotation()
}
