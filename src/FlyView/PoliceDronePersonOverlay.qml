import QtQuick
import QtMultimedia

import QGC as App
import QGroundControl.Controls

import "PoliceDroneHitTest.js" as HitTest

/// Person and vehicle boxes and head mosaics over the EO video; the counts are shown by
/// PoliceDroneAiPanel. The detector reports boxes normalised in the decoded frame, so they are
/// mapped through the VideoOutput's contentRect, which also accounts for the crop when the
/// panel fills with PreserveAspectCrop.
Item {
    id: root

    required property VideoOutput videoOutput

    /// Mosaic covers the top of each person box, where the face is at drone altitudes.
    readonly property real  headFraction: 0.2
    /// Pool size, and so the most boxes and mosaics drawn at once. A crowd seen from the
    /// air runs to a few dozen, and a pool of 16 drew only the first tiles' worth.
    readonly property int   maxBoxes:     128
    readonly property color markColor:    "#00ff00"
    readonly property color vehicleColor: "#4cc9f0"

    /// The box the pod is following, normalised in the frame, or an empty rect. A detection that
    /// sits under it is not drawn: the operator picked one box and it changed colour, and two
    /// marks on one person reads as two people.
    property rect trackedRect: Qt.rect(0, 0, 0, 0)

    readonly property var  _boxes:        App.PersonDetector.boxes
    readonly property var  _vehicleBoxes: App.PersonDetector.vehicleBoxes
    readonly property rect _content:      videoOutput.contentRect

    /// Results older than this are hidden so a stalled stream does not leave boxes frozen.
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
    /// null. See PoliceDroneHitTest.js for the rules.
    function boxAt(x, y) {
        if (!visible) {
            return null
        }
        return HitTest.boxAt([_boxes, _vehicleBoxes], maxBoxes, _content, width, height,
                             ScreenTools.minTouchPixels, x, y)
    }

    // A plain outlined box around one detection, drawn inside the item's bounds.
    component MarkBox: Rectangle {
        color:        "transparent"
        border.color: markBoxColor
        // Proportional, not a fixed weight: a crowd seen from the air is dozens of ten pixel
        // boxes, and a three pixel stroke on those paints the picture over.
        border.width: Math.max(1, Math.min(ScreenTools.defaultFontPixelHeight * 0.15,
                                           Math.min(width, height) * 0.08))

        property color markBoxColor
    }

    /// True when @a box is mostly covered by the pod's tracked box.
    function _isTracked(box) {
        if ((trackedRect.width <= 0) || (trackedRect.height <= 0) || (box.width <= 0)) {
            return false
        }
        const w = Math.max(0, Math.min(box.x + box.width,  trackedRect.x + trackedRect.width)  - Math.max(box.x, trackedRect.x))
        const h = Math.max(0, Math.min(box.y + box.height, trackedRect.y + trackedRect.height) - Math.max(box.y, trackedRect.y))
        return (w * h) > (0.5 * box.width * box.height)
    }

    Repeater {
        // Fixed pool: a person the detector loses for a frame hides its delegate instead of
        // destroying and rebuilding it, and its mosaic layer, at detector rate.
        model: root.maxBoxes

        delegate: Item {
            id: person
            required property int index
            readonly property bool detected: index < root._boxes.length
            readonly property rect box:      detected ? root._boxes[index] : Qt.rect(0, 0, 0, 0)

            visible: detected && !root._isTracked(box)
            x:       root._content.x + box.x * root._content.width
            y:       root._content.y + box.y * root._content.height
            width:   box.width * root._content.width
            height:  box.height * root._content.height

            // One box per detected person, so the count on the AI panel can be checked by eye.
            MarkBox {
                anchors.fill: parent
                z:            1  // above the mosaic, which covers the top edge otherwise
                markBoxColor: root.markColor
            }

            // The video re-rendered into a fixed 8x4 texture and drawn back unsmoothed: a
            // mosaic with no shader code, whose layer is allocated once per delegate because
            // the texture size does not follow the box.
            ShaderEffectSource {
                width:       person.width
                height:      person.height * root.headFraction
                sourceItem:  root.videoOutput
                sourceRect:  Qt.rect(person.x, person.y, width, height)
                textureSize: Qt.size(8, 4)
                smooth:      false
                live:        true
            }
        }
    }

    Repeater {
        // Same fixed pool as the persons, without the mosaic: vehicles are marked, not hidden.
        model: root.maxBoxes

        delegate: MarkBox {
            required property int index
            readonly property bool detected: index < root._vehicleBoxes.length
            readonly property rect box:      detected ? root._vehicleBoxes[index] : Qt.rect(0, 0, 0, 0)

            visible: detected
            x:       root._content.x + box.x * root._content.width
            y:       root._content.y + box.y * root._content.height
            width:   box.width * root._content.width
            height:  box.height * root._content.height
            markBoxColor: root.vehicleColor
        }
    }
}
