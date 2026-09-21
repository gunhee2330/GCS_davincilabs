import QtQuick
import QtTest

import "../../../../src/FlyView/PoliceDroneHitTest.js" as HitTest

/// Box pick rules for the AI tracker bridge: person over vehicle, smaller over larger, finger-size
/// hit area for tiny boxes, cropped-off boxes not pickable.
TestCase {
    name: "PoliceDroneHitTest"

    // A 16:9 frame in a 16:9 panel: contentRect equals the panel.
    readonly property rect fit:   Qt.rect(0, 0, 1600, 900)
    readonly property int  touch: 48

    function pick(groups, content, width, height, x, y) {
        return HitTest.boxAt(groups, 16, content, width, height, touch, x, y)
    }

    function test_insideAndOutside() {
        const person = [Qt.rect(0.5, 0.5, 0.1, 0.2)]  // panel px 800..960 x 450..630
        const hit = pick([person, []], fit, 1600, 900, 850, 500)
        verify(hit !== null)
        compare(hit.x, 0.5)
        compare(pick([person, []], fit, 1600, 900, 700, 500), null)
    }

    function test_personBeatsVehicleAndSmallerBeatsLarger() {
        const persons  = [Qt.rect(0.40, 0.40, 0.20, 0.30), Qt.rect(0.45, 0.45, 0.05, 0.10)]
        const vehicles = [Qt.rect(0.30, 0.30, 0.40, 0.50)]
        const hit = pick([persons, vehicles], fit, 1600, 900, 750, 450)
        compare(hit.width, 0.05)
        // Off the persons, still on the bus.
        compare(pick([persons, vehicles], fit, 1600, 900, 500, 300).width, 0.40)
    }

    function test_tinyBoxGetsFingerSizedHitArea() {
        const tiny = [Qt.rect(0.5, 0.5, 0.0025, 0.005)]  // 4 x 4.5 px at (800, 450)
        verify(pick([tiny, []], fit, 1600, 900, 820, 468) !== null)  // 20 px off, inside 48 px touch
        compare(pick([tiny, []], fit, 1600, 900, 830, 450), null)    // 30 px off
    }

    function test_croppedOffBoxIsSkipped() {
        // 16:9 frame filling a 16:10 screen with PreserveAspectCrop: 142 px cropped each side.
        const crop = Qt.rect(-142, 0, 2844, 1600)
        const persons  = [Qt.rect(0.0425, 0.5, 0.004, 0.012)]  // entirely in the cropped strip
        const vehicles = [Qt.rect(0.0, 0.4, 0.2, 0.3)]
        compare(pick([persons, vehicles], crop, 2560, 1600, 2, 810).width, 0.2)
    }

    function test_dragBoxIsDirectionFreeAndNormalisedThroughContentRect() {
        const a = Qt.point(400, 270)
        const b = Qt.point(1200, 720)
        const forward  = HitTest.dragBox(a, b, fit, 1600, 900, touch)
        const backward = HitTest.dragBox(b, a, fit, 1600, 900, touch)
        fuzzyCompare(forward.left,   0.25, 1e-6)
        fuzzyCompare(forward.top,    0.30, 1e-6)
        fuzzyCompare(forward.right,  0.75, 1e-6)
        fuzzyCompare(forward.bottom, 0.80, 1e-6)
        compare(JSON.stringify(backward), JSON.stringify(forward))
    }

    function test_dragBoxIsClampedToTheVisiblePartOfTheFrame() {
        // 16:9 frame filling a 16:10 screen with PreserveAspectCrop: 142 px cropped each side.
        const crop = Qt.rect(-142, 0, 2844, 1600)
        // Dragged off both edges: the box stops at the panel, and the panel edges are 142 px into
        // the frame on the left and 142 px short of its right edge.
        const box = HitTest.dragBox(Qt.point(-500, -500), Qt.point(3000, 2000), crop, 2560, 1600, touch)
        fuzzyCompare(box.left,   142 / 2844, 1e-6)
        fuzzyCompare(box.top,    0.0,        1e-6)
        fuzzyCompare(box.right,  (142 + 2560) / 2844, 1e-6)
        fuzzyCompare(box.bottom, 1.0,        1e-6)
    }

    function test_dragBoxRejectsTooSmallAndOffPictureDrags() {
        // A side under the minimum, in either axis.
        compare(HitTest.dragBox(Qt.point(400, 270), Qt.point(400 + touch - 1, 600), fit, 1600, 900, touch), null)
        compare(HitTest.dragBox(Qt.point(400, 270), Qt.point(900, 270 + touch - 1), fit, 1600, 900, touch), null)
        // On the minimum exactly is a selection.
        verify(HitTest.dragBox(Qt.point(400, 270), Qt.point(400 + touch, 270 + touch), fit, 1600, 900, touch) !== null)
        // Entirely outside the panel, and no picture at all - the state before the first frame.
        compare(HitTest.dragBox(Qt.point(2000, 270), Qt.point(2400, 700), fit, 1600, 900, touch), null)
        compare(HitTest.dragBox(Qt.point(400, 270), Qt.point(900, 700), Qt.rect(0, 0, 0, 0), 1600, 900, 0), null)
    }

    function test_onlyDrawnBoxesArePickable() {
        const boxes = []
        for (let i = 0; i < 17; ++i) {
            boxes.push(Qt.rect(i / 20, 0.5, 0.02, 0.1))
        }
        verify(pick([boxes, []], fit, 1600, 900, 15 / 20 * 1600 + 10, 500) !== null)
        compare(pick([boxes, []], fit, 1600, 900, 16 / 20 * 1600 + 10, 500), null)
    }
}
