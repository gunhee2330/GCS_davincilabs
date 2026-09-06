import QtQuick
import QtMultimedia

import QGC as App
import QGroundControl.Controls

import "PoliceDroneHitTest.js" as HitTest

/// Person and vehicle brackets and head mosaics over the EO video; the counts are shown by
/// PoliceDroneAiPanel. The detector reports boxes normalised in the decoded frame, so they are
/// mapped through the VideoOutput's contentRect, which also accounts for the crop when the
/// panel fills with PreserveAspectCrop.
Item {
    id: root

    required property VideoOutput videoOutput

    /// Mosaic covers the top of each person box, where the face is at drone altitudes.
    readonly property real  headFraction: 0.2
    /// Pool size, and so the most brackets and mosaics drawn at once. A crowd seen from the
    /// air runs to a few dozen, and a pool of 16 drew only the first tiles' worth.
    readonly property int   maxBoxes:     128
    readonly property color markColor:    "#ffd166"
    readonly property color vehicleColor: "#4cc9f0"

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

    // Four corner brackets around one detection, drawn inside the item's bounds.
    component Brackets: Item {
        id: brackets

        property color color

        readonly property real _corner: Math.min(width, height) * 0.28
        // Proportional, not a fixed weight: a crowd seen from the air is dozens of ten pixel
        // boxes, and a three pixel stroke on those paints the picture over.
        readonly property real _stroke: Math.max(1, Math.min(ScreenTools.defaultFontPixelHeight * 0.15,
                                                             Math.min(width, height) * 0.08))

        Repeater {
            model: 4

            Item {
                required property int index
                readonly property bool _right:  index % 2 === 1
                readonly property bool _bottom: index > 1

                x:      _right  ? brackets.width  - width  : 0
                y:      _bottom ? brackets.height - height : 0
                width:  brackets._corner
                height: brackets._corner

                Rectangle {
                    width:  parent.width
                    height: brackets._stroke
                    y:      parent._bottom ? parent.height - height : 0
                    color:  brackets.color
                }

                Rectangle {
                    width:  brackets._stroke
                    height: parent.height
                    x:      parent._right ? parent.width - width : 0
                    color:  brackets.color
                }
            }
        }
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

            visible: detected
            x:       root._content.x + box.x * root._content.width
            y:       root._content.y + box.y * root._content.height
            width:   box.width * root._content.width
            height:  box.height * root._content.height

            // Corner brackets instead of a hairline box: each detected person reads as one
            // bracketed target, so the count on the AI panel can be checked by eye.
            Brackets {
                anchors.fill: parent
                z:            1  // above the mosaic, which covers the top brackets otherwise
                color:        root.markColor
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

        delegate: Brackets {
            required property int index
            readonly property bool detected: index < root._vehicleBoxes.length
            readonly property rect box:      detected ? root._vehicleBoxes[index] : Qt.rect(0, 0, 0, 0)

            visible: detected
            x:       root._content.x + box.x * root._content.width
            y:       root._content.y + box.y * root._content.height
            width:   box.width * root._content.width
            height:  box.height * root._content.height
            color:   root.vehicleColor
        }
    }
}
