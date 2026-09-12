import QtQuick

import QGroundControl
import QGroundControl.Controls

/// The proximity sensors as a glow on the four edges of the map, for an operator whose eyes are on
/// the map rather than on the compass.
///
/// The eight sectors fold onto four edges: an edge takes the closest sector pointing within 45
/// degrees of it. A corner sector therefore lights both of its edges, which is wanted - an obstacle
/// off the nose-right startles less when both edges answer than when the operator has to work out
/// which single edge was chosen.
///
/// The sectors are the airframe's and the map is north-up, so they are turned by the heading before
/// folding, as PoliceDroneProximityRing does over the compass dial. The GeoMap engine's camera can
/// be twisted off north on top of that; GeoMapVehicleItem turns the aircraft icon by the same sum,
/// so glow and icon cannot disagree.
Item {
    id:         root
    objectName: "policeDroneObstacleGlow"

    /// The live map engine item, for the GeoMap camera's own rotation. The QtLocation engine has no
    /// geoMap and is always north-up.
    property var mapItem: null

    // Nothing to draw before the aircraft reports an obstacle, and nothing worth drawing on the
    // ground: a parked airframe reads close on every side and would cry wolf before every flight.
    visible: proximityValues.telemetryAvailable && _vehicle && _vehicle.armed

    readonly property var _vehicle: QGroundControl.multiVehicleManager.activeVehicle

    // Fractions of maxDistance, as on the ring. Above _warnRatio nothing is drawn at all.
    readonly property real _warnRatio: 0.6
    readonly property real _badRatio:  0.3

    // A share of the map's own width, so the band keeps its weight on a 7in controller and on a
    // desk monitor alike.
    readonly property real _thickness: width * 0.09

    readonly property real _screenRotation: {
        const heading = (_vehicle && !isNaN(_vehicle.heading.rawValue)) ? _vehicle.heading.rawValue : 0
        return heading + ((mapItem && mapItem.geoMap) ? mapItem.geoMap.camera.heading : 0)
    }

    /// Closest reading per edge, as a fraction of maxDistance: top, right, bottom, left. NaN where
    /// no sector bearing on that edge reported anything.
    readonly property var _edgeRatios: {
        const ratios = [NaN, NaN, NaN, NaN]
        const maxDistance = proximityValues.maxDistance
        if (!(maxDistance > 0)) {
            return ratios
        }
        for (let sector = 0; sector < 8; ++sector) {
            const distance = proximityValues.rgRotationValues[sector]
            // A sector this airframe does not carry arrives as NaN and leaves its edges dark.
            if (isNaN(distance)) {
                continue
            }
            const ratio = distance / maxDistance
            const screenAngle = (sector * 45) + root._screenRotation
            for (let edge = 0; edge < 4; ++edge) {
                const offset = Math.abs(((screenAngle - (edge * 90)) % 360 + 540) % 360 - 180)
                if (offset <= 45 && (isNaN(ratios[edge]) || ratio < ratios[edge])) {
                    ratios[edge] = ratio
                }
            }
        }
        return ratios
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    ProximityRadarValues {
        id:      proximityValues
        vehicle: root._vehicle
    }

    // Rectangle gradients are vertex-coloured scene graph geometry - two triangles a band, no
    // backing store, nothing re-uploaded while the map pans. A Shape or a Canvas would carry a
    // texture for what is a straight two-stop ramp.
    Repeater {
        model: 4

        delegate: Rectangle {
            id: band

            required property int index

            readonly property real _ratio: root._edgeRatios[index]
            readonly property bool _bad:   _ratio < root._badRatio

            // Top and bottom ramp down the screen, left and right across it.
            readonly property bool _alongY: (index % 2) === 0
            // Top and left are outermost at the start of their ramp, bottom and right at the end.
            readonly property bool _outerFirst: (index === 0) || (index === 3)

            // Orange rather than yellow for the warning band: the palette's colorYellow is a pure
            // #ffff00, which barely separates from a bright map.
            readonly property color _glow: Qt.alpha(_bad ? qgcPal.colorRed : qgcPal.colorOrange,
                                                    _bad ? 0.8 : 0.45)

            visible: !isNaN(_ratio) && (_ratio <= root._warnRatio)
            x:       index === 1 ? root.width  - root._thickness : 0
            y:       index === 2 ? root.height - root._thickness : 0
            width:   _alongY ? root.width  : root._thickness
            height:  _alongY ? root._thickness : root.height

            gradient: Gradient {
                orientation: band._alongY ? Gradient.Vertical : Gradient.Horizontal
                // The far stop is the same colour at zero alpha rather than "transparent": ramping
                // to a transparent black would grey the middle of the band.
                GradientStop { position: 0; color: band._outerFirst ? band._glow : Qt.alpha(band._glow, 0) }
                GradientStop { position: 1; color: band._outerFirst ? Qt.alpha(band._glow, 0) : band._glow }
            }

            // Metres for the close band only, on the inner edge of its own band.
            Rectangle {
                // The bottom band's middle is buried: the compass, the dials and the telemetry bar
                // all stack on the map's bottom edge, and this glow draws under them. Those leave a
                // margin clear at the band's right end at every window width, so the pill goes
                // there, centred in the band's own depth.
                readonly property bool _bottomBand: band.index === 2

                readonly property real _centreX: !band._alongY ? (band._outerFirst ? band.width : 0)
                                                              : (_bottomBand ? band.width - (width / 2) - ScreenTools.defaultFontPixelWidth
                                                                             : band.width / 2)
                readonly property real _centreY: (band._alongY && !_bottomBand) ? band.height
                                                                               : (band.height / 2)

                visible: band._bad
                x:       _centreX - (width  / 2)
                y:       _centreY - (height / 2)
                width:   distanceLabel.implicitWidth  + ScreenTools.defaultFontPixelWidth
                height:  distanceLabel.implicitHeight + (ScreenTools.defaultFontPixelHeight * 0.25)
                radius:  ScreenTools.defaultFontPixelHeight * 0.2
                color:   Qt.alpha(qgcPal.window, 0.8)

                QGCLabel {
                    id:               distanceLabel
                    anchors.centerIn: parent
                    color:            qgcPal.colorRed
                    font.bold:        true
                    // One decimal: the fact's own valueString carries two, more precision than a
                    // proximity sensor earns and a wider pill for no gain.
                    text:             (band._ratio * proximityValues.maxDistance).toFixed(1) + " m"
                }
            }
        }
    }
}
