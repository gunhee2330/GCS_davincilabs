import QtQuick
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// Detection card: how many of each class the pod's AI module is seeing. Counts only - tracking
/// and follow state live where the operator acts on them, drawn as chips on the AI window's own
/// picture. Styled like the telemetry bar it sits beside so the two read as one instrument row.
/// The switch itself lives in the camera strip.
///
/// The numbers come from the module's undocumented 0xD5 push, not from the on-device detector.
/// That command reports a tally per class and no coordinates at all, which is why the boxes on
/// screen are the module's own, drawn into its RTSP feed, and why the detector is down to face
/// mosaics with no count of its own to disagree with these.
Item {
    id: root

    readonly property real _em: ScreenTools.defaultFontPixelHeight

    /// The module's numbers mean something only while it says so. countsValid already covers all
    /// three ways they go bad - link down, counting switched off, pushes stopped arriving - so
    /// this card no longer keeps a staleness timer of its own.
    readonly property bool _live: App.SiyiAiController.countsValid

    /// Numbers rise at once but fall only on the settle tick, so an object the module misses for
    /// a frame does not make them flicker. -1 is the controller's "unknown", not a count, and is
    /// the resting value: a card that starts at 0 claims an empty scene it has never looked at.
    property int _persons:  -1
    property int _vehicles: -1
    property int _fires:    -1
    property int _smokes:   -1
    property int _boats:    -1

    /// The saturation flag that came with the held number above, not the module's current one.
    /// A held 255 paired with a live flag prints a bare "255" the moment the next push reports
    /// 250: the number is still the max-held 255 but the flag has already fallen, so the card
    /// claims an exact headcount for a tally the module only ever said was "at least 255". A
    /// crowd oscillating around the byte ceiling sits in that state half the time.
    property bool _personsSat:  false
    property bool _vehiclesSat: false
    property bool _firesSat:    false
    property bool _smokesSat:   false
    property bool _boatsSat:    false

    /// One band for every value, so the row keeps its height whatever a value measures.
    readonly property real _valueHeight: Math.max(18, _em * 1.25)

    implicitWidth:  row.implicitWidth + _em
    implicitHeight: Math.max(ScreenTools.minTouchPixels, _em * 2.9)

    /// A dash for a count the module cannot give, a number for one it can.
    ///
    /// -1 means the loaded model has no class of that kind at all, so there is nothing to sum and
    /// the empty sum is 0 - which would read as "none in view" from the middle of a crowd.
    ///
    /// "255+" marks the tally sitting exactly on the ceiling of the module's unsigned byte. It
    /// does NOT catch wraparound and cannot: the firmware accumulates into that byte and lets it
    /// wrap, so a crowd of three hundred arrives on the wire as 44 with nothing to tell it apart
    /// from 44 people, and this card prints 44. The marker means "at least 255", never "an
    /// accurate headcount". Same limit as SiyiAiController.h's personCountSaturated.
    function _display(count, saturated) {
        if (count < 0)  return "–"
        return saturated ? qsTr("255+") : count
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    Connections {
        target: App.SiyiAiController

        // Still the max-hold the on-device detector needed, with only the source swapped: 0xD5 is
        // pushed once per inference frame, so an object the module drops for a frame blinks the
        // raw number exactly as the detector's misses did. Rises land at once, falls wait for the
        // settle tick below.
        //
        // Only a real count is worth holding: feeding -1 into a max against a held 0 would swallow
        // the unknown and print a confident "0", and holding the last maximum across a link drop
        // would show it again for a tick when the link returns. The saturation flag is adopted
        // with the number and only with it - the two are one reading.
        function onCountsChanged() {
            const ai = App.SiyiAiController
            if (!ai.countsValid || (ai.personCount < 0)) {
                root._persons = -1
                root._personsSat = false
            } else if (ai.personCount >= root._persons) {
                root._persons = ai.personCount
                root._personsSat = ai.personCountSaturated
            }
            if (!ai.countsValid || (ai.vehicleCount < 0)) {
                root._vehicles = -1
                root._vehiclesSat = false
            } else if (ai.vehicleCount >= root._vehicles) {
                root._vehicles = ai.vehicleCount
                root._vehiclesSat = ai.vehicleCountSaturated
            }
            if (!ai.countsValid || (ai.fireCount < 0)) {
                root._fires = -1
                root._firesSat = false
            } else if (ai.fireCount >= root._fires) {
                root._fires = ai.fireCount
                root._firesSat = ai.fireCountSaturated
            }
            if (!ai.countsValid || (ai.smokeCount < 0)) {
                root._smokes = -1
                root._smokesSat = false
            } else if (ai.smokeCount >= root._smokes) {
                root._smokes = ai.smokeCount
                root._smokesSat = ai.smokeCountSaturated
            }
            if (!ai.countsValid || (ai.boatCount < 0)) {
                root._boats = -1
                root._boatsSat = false
            } else if (ai.boatCount >= root._boats) {
                root._boats = ai.boatCount
                root._boatsSat = ai.boatCountSaturated
            }
        }
    }

    Timer {
        interval:    1000
        repeat:      true
        running:     root._live
        onTriggered: {
            root._persons     = App.SiyiAiController.personCount
            root._personsSat  = App.SiyiAiController.personCountSaturated
            root._vehicles    = App.SiyiAiController.vehicleCount
            root._vehiclesSat = App.SiyiAiController.vehicleCountSaturated
            root._fires       = App.SiyiAiController.fireCount
            root._firesSat    = App.SiyiAiController.fireCountSaturated
            root._smokes      = App.SiyiAiController.smokeCount
            root._smokesSat   = App.SiyiAiController.smokeCountSaturated
            root._boats       = App.SiyiAiController.boatCount
            root._boatsSat    = App.SiyiAiController.boatCountSaturated
        }
    }

    // Same surface as TelemetryValuesBar.
    Rectangle {
        anchors.fill: parent
        color:        qgcPal.window
        radius:       ScreenTools.defaultFontPixelWidth / 2
        opacity:      0.75
        // Keeps a tap on the strip from reaching the map underneath, as the camera windows do.
        MouseArea { anchors.fill: parent }
    }

    component Stat: Column {
        id: stat
        property string label
        property string value
        property color  dot

        // The card is given a width by the dashboard so it lines up with the telemetry bar under
        // it; sharing that width out evenly is what lets the card be narrowed without the stats
        // spilling past its edge.
        Layout.fillWidth: true

        spacing: root._em * 0.15

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing:                  root._em * 0.28

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width:                  root._em * 0.36
                height:                 root._em * 0.36
                radius:                 width / 2
                color:                  stat.dot
            }

            Text {
                color:          qgcPal.text
                opacity:        0.7
                font.pixelSize: Math.max(12, root._em * 0.62)
                text:           stat.label
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            height:                   root._valueHeight
            verticalAlignment:        Text.AlignVCenter
            color:                    qgcPal.text
            // Every value on this strip is a count now, so the digit face is the only one left.
            font.family:              "Open Sans"
            font.weight:              Font.DemiBold
            font.pixelSize:           Math.max(16, root._em * 1.15)
            text:                     stat.value
        }
    }

    component Divider: Rectangle {
        Layout.alignment:       Qt.AlignVCenter
        Layout.preferredWidth:  1
        Layout.preferredHeight: root._em * 1.8
        color:                  qgcPal.text
        opacity:                0.15
    }

    RowLayout {
        id:                  row
        anchors.fill:        parent
        anchors.leftMargin:  root._em * 0.35
        anchors.rightMargin: root._em * 0.35
        spacing:             root._em * 0.3

        Stat {
            label: qsTr("인원")
            value: root._live ? root._display(root._persons, root._personsSat) : "–"
            dot:   "#e0a800"
        }

        Divider {}

        // The stat stays whatever the module can see: the slot is the delivery's, not the loaded
        // model's, and a module carrying a person-only model reads as a dash here rather than
        // vanishing and moving everything beside it.
        Stat {
            label: qsTr("차량")
            value: root._live ? root._display(root._vehicles, root._vehiclesSat) : "–"
            dot:   "#1f9fd0"
        }

        Divider {}

        Stat {
            label: qsTr("화재")
            value: root._live ? root._display(root._fires, root._firesSat) : "–"
            dot:   "#ff5b3a"
        }

        Divider {}

        Stat {
            label: qsTr("연기")
            value: root._live ? root._display(root._smokes, root._smokesSat) : "–"
            dot:   "#c8b04a"
        }

        Divider {}

        Stat {
            label: qsTr("보트")
            value: root._live ? root._display(root._boats, root._boatsSat) : "–"
            dot:   "#2ec4b6"
        }

        // Tracking and follow state used to hold two more slots here. They are gone, and this
        // strip is counts only. Neither was a count, and both now sit on the picture they are
        // happening on: a pair of chips in the AI window's far corner, beside the tracked
        // target's own box and its release button. Seven slots ran this strip across half a
        // seven-inch screen and put the numbers under the fullscreen hint, and follow in
        // particular read the same word all flight on a gimbal whose firmware has no follow
        // command at all. A slot that never changes is what teaches an operator to stop reading
        // the strip that carries the numbers they are here for.
    }
}
