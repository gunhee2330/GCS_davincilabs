import QtQuick
import QtMultimedia

import QGC as App
import QGroundControl.Controls

import "PoliceDroneHitTest.js" as HitTest

/// Face mosaics over the EO video, and the box a long press snaps to.
///
/// Detection boxes are no longer drawn here. The AI module marks and counts objects itself and
/// burns those marks into its own RTSP feed, so ours on top would double every object on screen.
/// What the module will not hand over is coordinates - its undocumented 0xD5 push carries
/// per-class tallies and nothing else - so the on-device detector stays for the two things that
/// need a rectangle in this process: where to cover a face, and what a long press landed on.
///
/// Two limits, both of which get asked about after an incident:
///  1. This mosaic never reaches a recording. It is drawn over the VideoOutput, while the recorder
///     branches off the tee ahead of the decoder and muxes the RTSP bitstream untouched. Masking
///     the file needs an element in the GStreamer pipeline, which this is not.
///  2. It covers only what the detector found. Someone it misses is a face left on screen. This is
///     an aid; it must not be described, here or in the UI, as protection.
///
/// Pixelation rather than a blur, because a Gaussian blur is partly invertible by deconvolution
/// and "it can be recovered" is an objection that actually lands when the personal-data handling
/// has to be justified. A block is information that is gone, and its size is the evidence of how
/// much went.
Item {
    id: root

    required property VideoOutput videoOutput

    /// How much of a person box, measured from the top, the mosaic covers. Generous on purpose:
    /// what fraction of a body reads as head depends on the look-down angle, which moves with
    /// altitude and gimbal pitch, and a mosaic stopping short of the chin leaves a face. Covering
    /// too much costs picture and nothing else. Field-tune this one number, nowhere else.
    readonly property real headFraction: 0.35

    /// Pool size, and so the most mosaics drawn at once. Each is its own render target, so the cap
    /// is a budget on render-target switches rather than on fill rate; a crowd seen from the air
    /// runs to a few dozen, and a pool of 16 covered only the first tiles' worth.
    readonly property int maxBoxes: 128

    /// Largest first once the cap bites: a big box is a near person, and a near face is the one
    /// that is actually identifiable in the picture. The detector orders by score, which is not
    /// the same question, so the sort only happens when boxes would otherwise be dropped.
    readonly property var _boxes: {
        const boxes = App.PersonDetector.boxes
        if (boxes.length <= maxBoxes) {
            return boxes
        }
        return boxes.slice()
                    .sort((l, r) => (r.width * r.height) - (l.width * l.height))
                    .slice(0, maxBoxes)
    }

    readonly property rect _content: videoOutput.contentRect

    /// Results older than this are hidden, so a stalled stream does not leave mosaics sitting over
    /// a picture that has moved on.
    property bool _fresh: false

    clip:    true
    visible: enabled && App.PersonDetector.active && _fresh

    Connections {
        target: App.PersonDetector
        function onDetectionsChanged() {
            root._fresh = true
            staleTimer.restart()
        }
    }

    Timer {
        id: staleTimer
        interval:    1500
        onTriggered: root._fresh = false
    }

    /// The detected box under a point in this item's coordinates, as the detector's own rect, or
    /// null. Whole person boxes, not the head strips drawn below: the module follows whatever
    /// rectangle it is handed, and handing it a head makes it chase a head. See
    /// PoliceDroneHitTest.js for the rules.
    function boxAt(x, y) {
        if (!visible) {
            return null
        }
        return HitTest.boxAt([_boxes], maxBoxes, _content, width, height,
                             ScreenTools.minTouchPixels, x, y)
    }

    Repeater {
        // Fixed pool: a person the detector loses for a frame hides its delegate instead of
        // destroying and rebuilding it, and its render target, at detector rate.
        model: root.maxBoxes

        delegate: Item {
            id: person
            required property int index
            readonly property bool detected: index < root._boxes.length
            readonly property rect box:      detected ? root._boxes[index] : Qt.rect(0, 0, 0, 0)

            visible: detected
            x:       root._content.x + box.x * root._content.width
            y:       root._content.y + box.y * root._content.height
            width:   box.width * root._content.width
            height:  box.height * root._content.height

            // The video re-rendered into a fixed 8x8 texture and drawn back unsmoothed: a mosaic
            // with no shader code. Fixed rather than scaled with the box, so the same amount of
            // information is destroyed at 30 m as at 120 m - a block count pinned to screen pixels
            // would leave features intact whenever the aircraft came down.
            //
            // sourceRect is in the sourceItem's coordinate system while person.x/y are in this
            // overlay's. They agree only because PoliceDroneCameraPanel anchors the VideoOutput and
            // this overlay to fill the same parent; reparent either and this needs mapping.
            ShaderEffectSource {
                width:       person.width
                height:      person.height * root.headFraction
                sourceItem:  root.videoOutput
                sourceRect:  Qt.rect(person.x, person.y, width, height)
                textureSize: Qt.size(8, 8)
                smooth:      false
                live:        true
            }
        }
    }
}
