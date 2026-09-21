import QtQuick
import QtMultimedia

import QGroundControl
import QGroundControl.Controls

import "PoliceDroneHitTest.js" as HitTest

Item {
    id: root

    property string panelTitle
    property string panelDetail
    /// Windowed panels sit under a title bar that already names them, so the chips only come
    /// out full screen, where there is no bar and the operator needs to know what they are on.
    property bool   showChrome: true
    property string streamObjectName

    /// Draws the on-device detector's face mosaics.
    /// Only the stream the detector taps (videoContent) has results to draw.
    property bool personDetectionEnabled: false

    /// Draws the proximity ring around the centre of the picture. Only for a camera bolted to
    /// the airframe: the ring is vehicle-relative, so on a gimballed window its arcs would point
    /// wherever the pod happens to be looking.
    property bool proximityRingEnabled: false

    /// Enables AI target selection on this panel: a drag on the picture boxes a target.
    property bool targetPickEnabled: false

    /// Whether there is a target to let go of, which is all the release button below reacts to.
    property bool trackCancelEnabled: false

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

    /// The box a drag on the video selected, handed to the AI module as the target. Coordinates
    /// are normalised 0..1 across the video frame, which the controller scales into the stream's
    /// own resolution.
    signal targetBoxPicked(real left, real top, real right, real bottom)

    /// The operator letting the tracked target go, from the button on the picture.
    signal trackCancelRequested()

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

    /// A dot and a word for one piece of state: neon green while it is happening, grey while it
    /// is not - idle is not a fault, and a red resting state reads as one across a whole flight.
    /// The word alone says which state; whether it is on is the colour's job.
    component StateChip: Rectangle {
        property bool   lit
        property string label

        readonly property color _litColor: "#39ff14"

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
                color:                  lit ? _litColor : "#9aa3ab"
            }

            Text {
                // Read by the chip test, inside one chip's own subtree.
                objectName:             "policeCameraStateChipText"
                anchors.verticalCenter: parent.verticalCenter
                color:                  lit ? _litColor : "#9aa3ab"
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
        objectName:      "policeCameraStateChips"
        anchors.right:   parent.right
        anchors.top:     parent.top
        anchors.margins: 8
        spacing:         6
        visible:         (root.followActive !== null) || (root.trackingActive !== null)

        // Follow leads: it is the one that moves the airframe.
        StateChip {
            visible: root.followActive !== null
            lit:     root.followActive === true
            label:   qsTr("추종")
        }

        StateChip {
            visible: root.trackingActive !== null
            lit:     root.trackingActive === true
            label:   qsTr("추적")
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

    // Letting the target go, on the picture the operator is watching it on. The camera rail
    // carries the same command, but the rail sits at z 2 and a full screen camera at 20, so full
    // screen - which is where boxes get drawn - the rail's button is behind the picture.
    // Greyed rather than hidden, for the rail button's reason: the module drops hasTarget on a
    // 1.5 s gap in the target stream, and a button that comes and goes is one the operator
    // reaches for and misses.
    QGCButton {
        id:                   trackCancelButton
        objectName:           "policeTrackCancelButton"
        anchors.left:         parent.left
        anchors.bottom:       parent.bottom
        anchors.leftMargin:   ScreenTools.defaultFontPixelWidth
        anchors.bottomMargin: ScreenTools.defaultFontPixelHeight
        height:               Math.max(implicitHeight, ScreenTools.minTouchPixels)
        text:                 qsTr("추적해제")
        visible:              root.targetPickEnabled
        enabled:              root.trackCancelEnabled
        onClicked:            root.trackCancelRequested()
    }

    /// Whether \a pos, in panel pixels, is on the release button. A gesture that starts there is
    /// the button's: the drag handler is allowed to take a grab off an item and the tap handler
    /// holds a passive grab throughout, so neither leaves the button alone on its own.
    function _onTrackCancel(pos) {
        return trackCancelButton.visible &&
               trackCancelButton.contains(trackCancelButton.mapFromItem(root, pos))
    }

    // The box being dragged out, in the colour it keeps once the module is following it. No
    // resting border on the panel - it was the grey hairline boxing every camera in.
    Rectangle {
        objectName:   "policeTargetDragBox"
        readonly property rect _content: videoOutput.contentRect
        visible:      root._dragBox !== null
        x:            _content.x + (root._dragBox ? root._dragBox.left * _content.width  : 0)
        y:            _content.y + (root._dragBox ? root._dragBox.top  * _content.height : 0)
        width:        root._dragBox ? (root._dragBox.right  - root._dragBox.left) * _content.width  : 0
        height:       root._dragBox ? (root._dragBox.bottom - root._dragBox.top)  * _content.height : 0
        color:        "transparent"
        border.color: "#ff9500"
        border.width: Math.max(2, ScreenTools.defaultFontPixelWidth * 0.25)
    }

    /// The drag in progress, press point and current point in panel pixels. Kept here rather than
    /// read off the handler on release: the centroid is already reset by then.
    property point _dragFrom: Qt.point(0, 0)
    property point _dragTo:   Qt.point(0, 0)

    /// The box under the finger while it is down, normalised in the frame, or null. No minimum
    /// side: it is drawn from the first pixel, and the minimum is what decides on release.
    readonly property var _dragBox: (targetDrag.active && !root._onTrackCancel(root._dragFrom))
                                        ? HitTest.dragBox(root._dragFrom, root._dragTo,
                                                          videoOutput.contentRect, width, height, 0)
                                        : null

    function _commitDrag() {
        // Measured: the handler is allowed to take the grab off an item, so a drag that started
        // on the release button reached here as a box. The press point is what says whose gesture
        // this is - it is still the one the button was pressed at.
        if (root._onTrackCancel(root._dragFrom)) {
            return
        }
        const box = HitTest.dragBox(root._dragFrom, root._dragTo, videoOutput.contentRect,
                                    width, height, ScreenTools.minTouchPixels * 0.5)
        // targetPickEnabled follows the AI module's own state, so it can go false with the finger
        // still down - which drops the handler mid-drag. A box the operator never finished must
        // not go out on the way.
        if (!root.targetPickEnabled || !box) {
            return
        }
        root.targetBoxPicked(box.left, box.top, box.right, box.bottom)
    }

    // Drag out a box and the module starts on it as the finger comes up - it takes a rectangle
    // straight, so there is nothing to confirm. The threshold keeps a press that is really a tap
    // from reading as a one pixel box.
    DragHandler {
        id:                  targetDrag
        target:              null
        enabled:             root.targetPickEnabled
        dragThreshold:       ScreenTools.minTouchPixels * 0.4
        grabPermissions:     PointerHandler.CanTakeOverFromItems | PointerHandler.ApprovesTakeOverByAnything

        onCentroidChanged: {
            if (active) {
                root._dragTo = centroid.position
            }
        }

        onActiveChanged: {
            if (active) {
                root._dragFrom = centroid.pressPosition
                root._dragTo   = centroid.position
            }
        }

        // The finger coming up is what sends the box: that is the transition where the handler
        // hands the grab back. A grab taken away instead - the system's own edge gesture, another
        // control claiming the pointer - ends the drag as a cancel and sends nothing.
        onGrabChanged: (transition, point) => {
            if (transition === PointerDevice.UngrabExclusive) {
                root._commitDrag()
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy:   TapHandler.DragThreshold
        // Measured: this policy holds a passive grab, which the button taking the exclusive grab
        // does not take away, so a press on the release button arrived here as a tap as well and
        // took the panel full screen under the operator's finger.
        onTapped:        (point) => {
            if (!root._onTrackCancel(point.position)) {
                root.activated()
            }
        }
    }
}
