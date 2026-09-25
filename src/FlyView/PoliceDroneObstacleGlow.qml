import QtQuick

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// The proximity sensors as a glow on the four edges of the map, for an operator whose eyes are on
/// the map rather than on the compass.
///
/// The eight sectors fold onto four edges, exactly one edge each: a corner sector goes to the next
/// edge clockwise, so a sector never lights two edges.
///
/// Aircraft-relative, as the forward window's ring is: the top edge is the nose whatever the
/// heading or the map's own rotation. Only the compass ring turns with the heading.
Item {
    id:         root
    objectName: "policeDroneObstacleGlow"

    /// Extra distance from the map's top edge that the top band's number has to clear, or 0 when
    /// the spot under the top bar is free. Whatever the dashboard parks there draws over this
    /// glow, which sits at z -1, so the number steps below it rather than under it.
    property real topLabelInset: 0

    // Up for as long as the sensor is talking, on the ground as well: one forward lidar cannot
    // surround a parked airframe the way a full ring would, and seeing it alive before takeoff is
    // wanted. Down again as soon as the readings stop, which is what the monitor's staleness is
    // for. The bands inside still only light for something inside the warn band.
    visible: lidar.fresh

    readonly property var _vehicle: QGroundControl.multiVehicleManager.activeVehicle

    // Metres, as on the ring, and the same two numbers: the edge and the arc describe the same
    // obstacle, so a band that lit at a different distance from its own arc would read as a second
    // reading. See PoliceDroneProximityRing.qml for why the bands are absolute rather than a share
    // of the sensor's range.
    readonly property real _warnMetres: 10
    readonly property real _badMetres:  7

    // A share of the map's own width, so the band keeps its weight on a 7in controller and on a
    // desk monitor alike.
    readonly property real _thickness: width * 0.09

    /// Closest reading per edge in metres: top, right, bottom, left. NaN where no sector bearing on
    /// that edge reported anything.
    readonly property var _edgeDistances: {
        const distances = [NaN, NaN, NaN, NaN]
        for (let sector = 0; sector < 8; ++sector) {
            // Raw metres, as on the ring, and for the same reason: these thresholds are metres.
            const distance = lidar.sectorDistances[sector]
            // A sector this airframe does not carry, or one gone quiet, arrives as NaN and leaves
            // its edges dark. The sensor's reported maximum is not consulted, as on the ring.
            if (isNaN(distance) || (distance <= 0)) {
                continue
            }
            // Sector 0 (nose) top, 2 right, 4 bottom, 6 left.
            const edge = Math.round(sector / 2) % 4
            if (isNaN(distances[edge]) || distance < distances[edge]) {
                distances[edge] = distance
            }
        }
        return distances
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    App.PoliceLidarMonitor {
        id:      lidar
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

            readonly property real _distance: root._edgeDistances[index]
            readonly property bool _bad:      _distance < root._badMetres

            // Top and bottom ramp down the screen, left and right across it.
            readonly property bool _alongY: (index % 2) === 0
            // Top and left are outermost at the start of their ramp, bottom and right at the end.
            readonly property bool _outerFirst: (index === 0) || (index === 3)

            // Orange rather than yellow for the warning band: the palette's colorYellow is a pure
            // #ffff00, which barely separates from a bright map.
            readonly property color _glow: Qt.alpha(_bad ? qgcPal.colorRed : qgcPal.colorOrange,
                                                    _bad ? 0.8 : 0.45)

            visible: !isNaN(_distance) && (_distance <= root._warnMetres)
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

            // Metres for anything inside the warn band, on the band's own glow rather than in a
            // pill of its own: a dark box on top of it only adds a second shape to read. In the
            // band's colour with a dark outline, which holds over the glow and over a bright
            // satellite tile on the edges the glow leaves pale.
            Row {
                readonly property real _margin: ScreenTools.defaultFontPixelWidth

                // Which end of its own band the number sits at. The right and bottom bands hug
                // their outer edge, where the glow is strongest: below the camera column the map's
                // right edge is clear, and so is the bottom edge past the telemetry bar, which is
                // why the bottom number keeps to the band's right end.
                //
                // The top band hugs its outer edge too, just under the top bar, and steps down by
                // topLabelInset only while the dashboard has something parked there. The left
                // band keeps its inner edge: the left tool strip runs down the outer one as far
                // as the forward window, and nothing makes room for a number there.
                readonly property real _centreX: band._outerFirst
                    ? (band._alongY ? band.width / 2 : band.width)
                    : band.width - (width / 2) - _margin
                readonly property real _centreY: band._alongY
                    ? (band._outerFirst ? root.topLabelInset + _margin + (height / 2)
                                        : band.height - (height / 2) - _margin)
                    : band.height / 2

                x:       _centreX - (width  / 2)
                y:       _centreY - (height / 2)
                spacing: _margin * 0.35

                // The inset appears and goes with the control that caused it, so the number slides
                // the step rather than blinking to the other end of the band.
                Behavior on y { NumberAnimation { duration: 120 } }

                QGCLabel {
                    id:             distanceLabel
                    color:          "white"
                    font.bold:      true
                    font.pointSize: ScreenTools.largeFontPointSize
                    style:          Text.Outline
                    styleColor:     Qt.rgba(0, 0, 0, 0.75)
                    // One decimal: more is precision a proximity sensor has not earned and a
                    // wider number for no gain. Metres, which is what the monitor holds.
                    text:           band.index === 0 ? qsTr("전방 %1 m").arg(band._distance.toFixed(1))
                                                         : band._distance.toFixed(1)
                }

                QGCLabel {
                    anchors.baseline: distanceLabel.baseline
                    visible:          band.index !== 0
                    color:            Qt.alpha("white", 0.85)
                    style:            Text.Outline
                    styleColor:       distanceLabel.styleColor
                    text:             "m"
                }
            }
        }
    }
}
