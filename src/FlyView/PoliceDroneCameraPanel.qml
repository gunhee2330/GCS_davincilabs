import QtQuick
import QtMultimedia

import QGroundControl
import QGroundControl.Controls

import "PoliceDroneHitTest.js" as HitTest

Item {
    id: root

    property string panelTitle
    /// Drawn after the title inside the name chip, the way a window's chip carries its reading.
    property string panelTitleDetail
    property string panelDetail
    /// Windowed panels sit under a title bar that already names them, so the chips only come
    /// out full screen, where there is no bar and the operator needs to know what they are on.
    property bool   showChrome: true
    /// Big picture only: how far the dashboard's chrome reaches in from each side, so the chips
    /// land where they can be read.
    property real   chromeLeftInset: 0
    property real   chromeRightInset: 0
    property real   chromeTopInset: 0
    /// Big picture only: how far down the camera rail's handle reaches, and how far in its right
    /// edge sits, so the state chips line up under it instead of beside it on the top row.
    property real   stateChipsTopInset: 0
    property real   stateChipsRightInset: chromeRightInset
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

    /// Long press reads the temperature under the finger. Thermal only, and only full screen:
    /// the split window is too small to put a finger on a spot and mean it.
    property bool pointPickEnabled: false

    /// What to mark on the picture, as {x, y, color, label} with x and y in 0..1 frame
    /// coordinates. Drawn through contentRect, so a cropped picture still marks the right spot.
    property var markers: []

    /// Shows the release button on the picture. Set only while this panel is full screen.
    property bool trackCancelVisible: false

    property bool aiTargetVisible:      false
    property real aiTargetX:            0
    property real aiTargetY:            0
    property real aiTargetWidth:        0
    property real aiTargetHeight:       0
    property string aiTargetLabel:      qsTr("TARGET")
    /// Readout drawn along the bottom edge while a target is tracked: class, position, size,
    /// laser range. Empty hides it.
    property string aiTargetInfo:       ""

    /// The laser's aim point and its reading under it. Set on the zoom panel full screen with
    /// the laser switched on; drawn only with the chrome, so never on the small window.
    property bool   lrfOverlayVisible:  false
    property string lrfText:            ""
    /// Whether lrfText is a reading rather than the no-reading word, which is drawn dimmer.
    property bool   lrfHasReading:      false

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

    /// Long press on a panel with pointPickEnabled, in the same 0..1 frame coordinates.
    signal pointPicked(real normalisedX, real normalisedY)

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
        // Full screen the radius grows over five times and strokes at a share of it made a 33 px bar
        // beside a number still at its windowed size. The strokes stop at a cap the windowed ring
        // stays under, and the number grows with the ring up to one, so windowed nothing changes.
        boldStroke:         Math.min(ringRadius * 0.21, _strokeCap)
        warnStroke:         Math.min(ringRadius * 0.15, _strokeCap * 0.7)
        labelPointSize:     ScreenTools.defaultFontPointSize *
                            Math.max(1, Math.min(ringRadius * 0.22 / ScreenTools.defaultFontPixelHeight, 1.6))

        readonly property real _strokeCap: ScreenTools.defaultFontPixelHeight * 0.6
    }

    // Laser aim point: four thin arms with a gap at the centre, and the reading on a dark pill
    // under it. Sizes are the mockup's at the tablet's 45 px font height, as shares of it.
    Item {
        id:           lrfOverlay
        objectName:   "policeLrfOverlay"
        anchors.fill: parent
        visible:      root.showChrome && root.lrfOverlayVisible

        readonly property real _gap:   ScreenTools.defaultFontPixelHeight * 0.22
        readonly property real _arm:   ScreenTools.defaultFontPixelHeight * 0.67
        readonly property real _thick: Math.max(2, ScreenTools.defaultFontPixelHeight * 0.07)

        Repeater {
            model: 4

            Item {
                anchors.centerIn: parent
                width:            (lrfOverlay._gap + lrfOverlay._arm) * 2
                height:           width
                rotation:         index * 90

                // A dark edge under the white so the arm holds on sky and snow alike.
                Rectangle {
                    x:      parent.width / 2 + lrfOverlay._gap - 1.5
                    y:      (parent.height - height) / 2
                    width:  lrfOverlay._arm + 3
                    height: lrfOverlay._thick + 3
                    color:  "#96000000"

                    Rectangle {
                        anchors.centerIn: parent
                        width:            parent.width - 3
                        height:           parent.height - 3
                        color:            "white"
                    }
                }
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y:      parent.height / 2 + lrfOverlay._gap + lrfOverlay._arm + ScreenTools.defaultFontPixelHeight * 0.62
            width:  lrfLabel.implicitWidth + lrfLabel.font.pixelSize * 1.23
            height: lrfLabel.font.pixelSize * 1.62
            radius: height / 2
            color:  "#96000000"

            Text {
                id:               lrfLabel
                objectName:       "policeLrfOverlayText"
                anchors.centerIn: parent
                color:            root.lrfHasReading ? "white" : "#9aa3ab"
                font.pixelSize:   ScreenTools.defaultFontPixelHeight * 0.7
                text:             root.lrfText
            }
        }
    }

    Rectangle {
        objectName:     "policeCameraNameChip"
        anchors.left:   parent.left
        anchors.top:    parent.top
        anchors.margins: 8
        anchors.leftMargin: 8 + root.chromeLeftInset
        anchors.topMargin: 8 + root.chromeTopInset
        width:          panelHeading.implicitWidth + 18
        height:         panelHeading.implicitHeight + 10
        radius:         3
        color:          "#c0121b24"
        visible:        root.showChrome

        Row {
            id:             panelHeading
            anchors.centerIn: parent
            spacing:        6

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color:          "white"
                font.bold:      true
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.7
                text:           root.panelTitle
            }
            Text {
                objectName:     "policeCameraTitleDetail"
                anchors.verticalCenter: parent.verticalCenter
                color:          "#9fb2c4"
                // Title to reading in the same proportion as the window chip, 0.55 to 0.62.
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.62
                text:           root.panelTitleDetail
                visible:        root.panelTitleDetail.length > 0
            }
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
        anchors.rightMargin: 8 + root.stateChipsRightInset
        anchors.topMargin: 8 + Math.max(root.chromeTopInset, root.stateChipsTopInset)
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
        anchors.rightMargin: 8 + root.chromeRightInset
        anchors.topMargin: 8 + (stateChips.visible ? 0 : root.chromeTopInset)
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

    // Letting the target go, on the picture the operator is watching it on. Unused by the
    // dashboard now: the big picture sits under the camera rail, so the rail's 추적해제 stays
    // reachable and this one would only cover the picture.
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
        visible:              root.targetPickEnabled && root.trackCancelVisible
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

    // Long press reads a temperature. It rides beside the drag handler rather than inside it:
    // the two never run on the same panel - the thermal window does not pick AI targets - and a
    // threshold is what keeps a press that is really a tap from reading as a pick.
    TapHandler {
        enabled:            root.pointPickEnabled
        gesturePolicy:      TapHandler.DragThreshold
        onLongPressed: {
            const c = videoOutput.contentRect
            const p = point.position
            const nx = (p.x - c.x) / Math.max(1, c.width)
            const ny = (p.y - c.y) / Math.max(1, c.height)
            // Ignored outside the picture: with the whole frame shown the panel has bars down its
            // sides, and they are not part of what there is to read.
            if ((nx >= 0) && (nx <= 1) && (ny >= 0) && (ny <= 1)) {
                pickFlash.x = p.x - pickFlash.width / 2
                pickFlash.y = p.y - pickFlash.height / 2
                pickFlash.flash()
                root.pointPicked(nx, ny)
            }
        }
    }

    // Says the press was taken, at the moment it was taken. The reading itself is a round trip to
    // the pod away, and without this the panel answered a long press with nothing for a beat.
    Rectangle {
        id:      pickFlash
        width:   ScreenTools.minTouchPixels * 1.4
        height:  width
        radius:  width / 2
        color:   "transparent"
        border.color: "#ff3b30"
        border.width: 3
        opacity: 0
        z:       5

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

    // The marks, through contentRect like the target box, so a picture cropped to fill the screen
    // still has them on the right spot of it. A point the crop has cut off is left unmarked rather
    // than pinned to the edge, where it would claim a place it is not.
    Repeater {
        model: root.markers

        delegate: Item {
            id: marker
            required property var modelData
            readonly property rect _content: videoOutput.contentRect
            readonly property real _ring:    ScreenTools.defaultFontPixelHeight * 1.2
            // Labels read to the right of their mark, and swap sides near the right edge so a hot
            // spot in the corner does not push its reading off the screen.
            readonly property bool _labelLeft: x > root.width * 0.72

            x:       _content.x + modelData.x * _content.width
            y:       _content.y + modelData.y * _content.height
            visible: (x >= 0) && (y >= 0) && (x <= root.width) && (y <= root.height)
            z:       4

            Rectangle {
                anchors.centerIn: parent
                width:            marker._ring
                height:           width
                radius:           width / 2
                color:            "transparent"
                border.color:     marker.modelData.color
                border.width:     Math.max(2, width * 0.12)
            }

            Rectangle {
                anchors.centerIn: parent
                width:            Math.max(4, marker._ring * 0.22)
                height:           width
                radius:           width / 2
                color:            marker.modelData.color
            }

            Rectangle {
                x:      marker._labelLeft ? -width - marker._ring * 0.7 : marker._ring * 0.7
                // Beside the mark, and inside the panel: a reading on the bottom edge of the
                // frame ended up half off the screen.
                y:      Math.max(-marker.y, Math.min(-height / 2, root.height - marker.y - height))
                width:  markerLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 1.2
                height: markerLabel.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.3
                radius: 3
                color:  "#cc000000"

                Text {
                    id:               markerLabel
                    anchors.centerIn: parent
                    color:            marker.modelData.color
                    font.bold:        true
                    font.pixelSize:   Math.max(13, ScreenTools.defaultFontPixelHeight * 0.85)
                    text:             marker.modelData.label
                }
            }
        }
    }
}
