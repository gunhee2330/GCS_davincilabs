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

    function test_onlyDrawnBoxesArePickable() {
        const boxes = []
        for (let i = 0; i < 17; ++i) {
            boxes.push(Qt.rect(i / 20, 0.5, 0.02, 0.1))
        }
        verify(pick([boxes, []], fit, 1600, 900, 15 / 20 * 1600 + 10, 500) !== null)
        compare(pick([boxes, []], fit, 1600, 900, 16 / 20 * 1600 + 10, 500), null)
    }
}
