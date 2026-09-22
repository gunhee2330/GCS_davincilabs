import QtQuick
import QtQuick.Shapes

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// The eight proximity sensors drawn as a ring of arcs around a centre the caller places.
///
/// Vehicle-relative, like the sensors themselves: sector 0 is the nose. Where the top of the ring
/// is already the nose - the video window - the arcs are drawn as they come; over a dial that keeps
/// north at the top they are turned by the heading instead (northUp).
///
/// The bands are metres, set from how far this airframe needs to stop rather than from a share of
/// whatever range the fitted rangefinder happens to report. Refit the sensor and these numbers are
/// the ones to revisit; see _warnMetres.
Item {
    id:         root
    objectName: "policeDroneProximityRing"

    /// Outer bound of everything the ring paints, not the middle of the arcs: the compass hands in
    /// the rim of the pill the ring sits in, and a stroke centred on that rim would spill half its
    /// width onto the map and the attitude picture either side of it.
    property real ringRadius: 0

    /// Lets the caller suppress the ring where it would lie about direction - a gimballed
    /// window, or an instrument panel whose compass is not where this expects it.
    property bool active: true

    /// A faint circle under the arcs. On the compass it keeps the gauge legible as a gauge when
    /// nothing is close; over the video it would be one more thing between the operator and the
    /// picture, so that caller leaves it off.
    property bool showQuietRing: false

    /// Metres beside the arc, for the close band only. Room for it exists over the video.
    property bool showDistanceLabels: false

    /// Arc thickness, warn band and close band. A share of the radius by default, which is what an
    /// open video window wants; the compass hands in measured ones, having only the margin its pill
    /// leaves outside the dial face to paint in.
    property real warnStroke: ringRadius * 0.15
    property real boldStroke: ringRadius * 0.21

    /// A dark edge under each arc, so the colour still reads over a bright frame. The instrument
    /// pill has no bright frame to fight and no room to spend on one.
    property bool outlined: true

    /// Set when the item under the ring keeps north at the top, which QGC's compass dial does
    /// unless lockNoseUpCompass is set. The sectors are the airframe's, so on a north-up
    /// background they have to be turned by the heading to point at the obstacle.
    property bool northUp: false

    width:  (ringRadius + boldStroke) * 2
    height: width

    // Up for as long as the sensor is talking, on the ground as well: one forward lidar cannot
    // surround a parked airframe the way a full ring would, and the gauge being alive before
    // takeoff is how the operator knows the sensor is there at all. Down again as soon as the
    // readings stop, which is what the monitor's staleness is for.
    visible: active && lidar.fresh

    readonly property var _vehicle: QGroundControl.multiVehicleManager.activeVehicle

    // Metres, not a share of the sensor's range. Above _warnMetres nothing is drawn at all.
    //
    // A share of maxDistance moves the bands with whatever rangefinder is fitted, which reads well
    // until the fitted one is a TF Mini: it tops out at 12 m, so the old 0.6/0.3 put the warning at
    // 7.2 m and the red band at 3.6 m. At the 5 m/s this airframe flies, a stop the operator has to
    // start themselves needs about 7.5 m - a second to see the band and reach the stick, then 2.5 m
    // to shed the speed at MPC_ACC_HOR_MAX 5 m/s^2 - so red at 3.6 m arrives after the collision.
    //
    // 10 m and 7 m instead. 12 m is the sensor's own ceiling, where readings are least trustworthy
    // and a band would simply be lit whenever anything is in view, so the warning starts below it.
    readonly property real _warnMetres: 10
    readonly property real _badMetres:  7

    readonly property real _outlineWidth: outlined ? 2 : 0

    // Half the widest stroke in from ringRadius, outline included, so the whole ring paints inward
    // from its bound instead of straddling it.
    readonly property real _arcRadius: ringRadius - ((boldStroke + _outlineWidth) / 2)

    readonly property real _sectorSweepAngle:      360 / 8
    readonly property real _firstSectorStartAngle: -90 - (_sectorSweepAngle / 2)

    // Zero wherever the top of the ring is already the nose. ProximityRadarMapView turns its arcs
    // by the heading for the same reason: north-up background, airframe-frame sectors.
    readonly property real _headingRotation:
        (northUp && _vehicle && !isNaN(_vehicle.heading.rawValue)) ? _vehicle.heading.rawValue : 0

    function _sectorDistance(sectorIndex) {
        // Raw metres from the monitor, never a Fact value: a Fact hands QML the number already
        // converted to the app's horizontal distance unit, and the bands below are metres.
        const distance = lidar.sectorDistances[sectorIndex]
        // A sector the aircraft has never reported, or one that has gone quiet, comes through as
        // NaN and must stay blank: the ring covers eight directions, a given airframe rarely
        // carries eight sensors. The sensor's own declared maximum is not consulted either - a
        // TF Mini reports past its 12 m, out to 20 m in the flight logs, so the comparison would
        // discard real readings while catching nothing.
        if (isNaN(distance) || (distance <= 0)) {
            return NaN
        }
        return distance
    }

    function _sectorStroke(sectorIndex) {
        const distance = _sectorDistance(sectorIndex)
        if (isNaN(distance) || distance > _warnMetres) {
            return 0
        }
        return distance < _badMetres ? boldStroke : warnStroke
    }

    function _sectorColor(sectorIndex) {
        const distance = _sectorDistance(sectorIndex)
        if (isNaN(distance) || distance > _warnMetres) {
            return "transparent"
        }
        // Orange rather than yellow for the warning band: the palette's colorYellow is a pure
        // #ffff00, while colorOrange lands nearer the mockup's amber warning tone.
        return distance < _badMetres ? qgcPal.colorRed : qgcPal.colorOrange
    }

    function _sectorStartAngle(sectorIndex) {
        return _firstSectorStartAngle + (sectorIndex * _sectorSweepAngle)
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    App.PoliceLidarMonitor {
        id:      lidar
        vehicle: root._vehicle
    }

    Rectangle {
        anchors.centerIn: parent
        // A Rectangle border grows inwards while a Shape stroke straddles its radius, so the
        // circle is widened by half a stroke to put both bands on the one track.
        width:            (root._arcRadius + (root.warnStroke / 2)) * 2
        height:           width
        radius:           width / 2
        color:            "transparent"
        border.color:     Qt.alpha(qgcPal.text, 0.22)
        border.width:     root.warnStroke
        visible:          root.showQuietRing
    }

    // Shape rather than Canvas, as on the map: scene graph geometry with no backing store, and
    // the sensor values only change as fast as the aircraft sends them.
    Shape {
        id:           arcs
        anchors.fill: parent

        transform: Rotation {
            origin.x: arcs.width  / 2
            origin.y: arcs.height / 2
            angle:    root._headingRotation
        }

        // Sixteen paths for eight arcs: the first eight stroke the same arc in the palette's dark
        // map-widget border, slightly wider, so the colour on top still reads over a bright frame.
        // A ShapePath carries one stroke, and an outline is what an arc over live video needs.
        Instantiator {
            model: 16

            delegate: ShapePath {
                required property int index

                readonly property int  _sector:  index % 8
                readonly property bool _outline: index < 8
                readonly property real _width:   root._sectorStroke(_sector)

                strokeColor: _outline
                                 ? ((_width > 0 && root._outlineWidth > 0)
                                        ? Qt.alpha(qgcPal.mapWidgetBorderDark, 0.55) : "transparent")
                                 : root._sectorColor(_sector)
                strokeWidth: _width + (_outline ? root._outlineWidth : 0)
                fillColor:   "transparent"
                capStyle:    ShapePath.RoundCap

                PathAngleArc {
                    centerX:    arcs.width  / 2
                    centerY:    arcs.height / 2
                    radiusX:    root._arcRadius
                    radiusY:    radiusX
                    startAngle: root._sectorStartAngle(_sector)
                    sweepAngle: root._sectorSweepAngle
                }
            }

            onObjectAdded: (index, object) => arcs.data.push(object)
        }
    }

    Repeater {
        model: 8

        delegate: Rectangle {
            required property int index

            readonly property real _angle:       (-90 + root._headingRotation +
                                                 (index * root._sectorSweepAngle)) * Math.PI / 180
            // Inside the arc rather than outside it. The ring's own bounds stop at ringRadius plus
            // boldStroke, so an outward pill hangs past them - over the video window that put it
            // past the panel's top edge and on top of the toolbar above.
            readonly property real _labelRadius: root._arcRadius - root.boldStroke - (height / 2)

            visible: root.showDistanceLabels && (root._sectorDistance(index) < root._badMetres)
            x:       (root.width  / 2) + (Math.cos(_angle) * _labelRadius) - (width  / 2)
            y:       (root.height / 2) + (Math.sin(_angle) * _labelRadius) - (height / 2)
            width:   distanceLabel.implicitWidth  + ScreenTools.defaultFontPixelWidth
            height:  distanceLabel.implicitHeight + (ScreenTools.defaultFontPixelHeight * 0.25)
            radius:  ScreenTools.defaultFontPixelHeight * 0.2
            color:   Qt.alpha(qgcPal.window, 0.8)

            QGCLabel {
                id:               distanceLabel
                anchors.centerIn: parent
                color:            qgcPal.colorRed
                font.bold:        true
                // One decimal: more than that is precision a proximity sensor has not earned and
                // a wider pill for no gain. Metres, which is what the monitor holds.
                text:             lidar.sectorDistances[index].toFixed(1) + " m"
            }
        }
    }
}
