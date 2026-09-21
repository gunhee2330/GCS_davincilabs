import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.Toolbar

/// The police fly view's top bar.
///
/// Two regions, not three. The left is the verdict: the menu, the brand mark, the state banner
/// and the aircraft's messages. The right is every number, right-aligned in one cluster at one
/// size and one weight, so the differences between them are carried by colour alone. Nothing
/// sits in the middle: that space is where the confirm control and the warning banners drop in.
///
/// Every drawer opens under the item that was tapped, never under the bar or a row - the message
/// list under the message pictogram, the link pages under the RF group, the battery page under
/// the pack. showIndicatorDrawer positions the drawer on the item it is handed, so the item is
/// what is handed to it.
///
/// Nothing here reaches into its parent. Everything the bar reads is a property declared below
/// and bound by the dashboard, so the bar can be read on its own and moved without hunting for
/// what it was resolving up the scope chain.
Rectangle {
    id: bar

    // ------------------------------------------------------------------ what the bar reads
    property var    vehicle:       null
    /// { text, accent, blocked } - PoliceDroneDashboard._status.
    property var    status:        null
    /// The lowest pack, PoliceDroneDashboard._lowestBattery.
    property var    lowestBattery: null
    property real   batteryPercent: NaN
    property color  batteryColor:  "white"
    property bool   rcAvailable:   false
    property int    rcLevel:       0
    property bool   linkUp:        false
    property bool   gpsFixed:      false
    property string gpsFixText:    ""

    // -------------------------------------------------------------------- how it is drawn
    property real  iconSize:        PoliceBar.iconSize
    property real  labelSize:       12
    property real  valueSize:       PoliceBar.textSize
    property real  badgeSize:       10
    property real  menuButtonWidth: PoliceBar.menuButtonWidth
    property color labelColor:      "#9fb2c4"
    property color barColor:        PoliceBar.color
    property color alarmColor:      "#ff5b5b"
    property color warnColor:       "#ffb02e"
    property color idleColor:       "#8a9199"

    // ----------------------------------------------------- the drawers that already exist
    property Component gpsPage:           null
    property Component messagesPage:      null
    property Component linkSelectPage:    null
    property Component linkConnectedPage: null

    signal menuRequested()

    color: bar.barColor

    /// The gap between two items in the right cluster. Inside an item the gap is half of it,
    /// so a pictogram and the number it labels read as one chunk rather than as two items.
    readonly property real _itemGap:  ScreenTools.defaultFontPixelWidth * 1.1
    readonly property real _innerGap: ScreenTools.defaultFontPixelWidth * 0.5
    /// A touch target that still fits a bar shorter than the platform's finger size.
    readonly property real _touchHeight: Math.min(bar.height, ScreenTools.minTouchPixels)

    readonly property color _accent: bar.status ? bar.status.accent : bar.idleColor

    // Every piece of text on this bar, for the sake of one line: the family. A bare Text is
    // drawn in the platform's default face, which under a Korean locale sets its digits on
    // their own widths - and the banner reads "비행 중 02:14" once armed, so its width would
    // move every second and shuffle the row beside it. Both faces this app ships set their
    // digits on one width.
    component BarText: Text {
        font.family: "Open Sans"
    }

    component BarIcon: QGCColoredImage {
        property color tint: "white"

        anchors.verticalCenter: parent.verticalCenter
        width:                  bar.iconSize
        height:                 bar.iconSize
        color:                  tint
        fillMode:               Image.PreserveAspectFit
        sourceSize.height:      bar.iconSize
    }

    /// Four steps of signal, the filled ones in the group's colour and the rest ghosted.
    component BarGauge: Row {
        property int   level: 0
        property color tint:  "white"

        height:  bar.iconSize
        spacing: Math.max(2, bar.iconSize * 0.09)

        Repeater {
            model: 4

            Rectangle {
                required property int index
                anchors.bottom: parent.bottom
                width:          Math.max(3, bar.iconSize * 0.16)
                height:         bar.iconSize * (0.3 + index * 0.19)
                radius:         1
                color:          index < parent.level ? parent.tint : "#38ffffff"
            }
        }
    }

    /// Measured off the bar, not off the pictogram beside it: tied to the icon, a separator on
    /// a bar half again as tall as its icons is a stub floating in the middle of the gap.
    component BarSep: Rectangle {
        Layout.alignment:       Qt.AlignVCenter
        Layout.preferredWidth:  1
        Layout.preferredHeight: bar.height * 0.7
        color:                  "#26ffffff"
    }

    /// A cell on its side with the charge growing from the left, drawn rather than fetched: the
    /// fill is the reading, so the glyph says at a glance what the number beside it spells out.
    component BatteryGlyph: Item {
        id: glyph

        property color tint:    "white"
        /// 0..100, NaN when the pack reports no percentage - the body is then drawn empty.
        property real  percent: NaN

        anchors.verticalCenter: parent.verticalCenter
        width:                  bar.iconSize * 1.55
        height:                 bar.iconSize * 0.8

        Rectangle {
            id:                  body
            anchors.left:        parent.left
            anchors.top:         parent.top
            anchors.bottom:      parent.bottom
            anchors.right:       parent.right
            anchors.rightMargin: glyph.width * 0.125
            radius:              Math.max(1, glyph.height * 0.22)
            color:               "transparent"
            border.width:        Math.max(1, glyph.height * 0.11)
            border.color:        glyph.tint

            Rectangle {
                readonly property real _inset: body.border.width * 1.6

                anchors.left:           parent.left
                anchors.leftMargin:     _inset
                anchors.verticalCenter: parent.verticalCenter
                height:                 Math.max(0, parent.height - (_inset * 2))
                width:                  Math.max(0, (parent.width - (_inset * 2)) *
                                                    Math.max(0, Math.min(100, glyph.percent)) / 100)
                radius:                 Math.max(1, height * 0.2)
                color:                  glyph.tint
                visible:                !isNaN(glyph.percent)
            }
        }

        Rectangle {
            anchors.right:          parent.right
            anchors.verticalCenter: parent.verticalCenter
            width:                  glyph.width * 0.075
            height:                 glyph.height * 0.45
            radius:                 Math.max(1, width * 0.5)
            color:                  glyph.tint
        }
    }

    // The bar itself takes every tap that misses an item on it, so a press aimed at the bar
    // does not fall through to the map underneath.
    MouseArea { anchors.fill: parent }

    RowLayout {
        anchors.fill:        parent
        anchors.leftMargin:  PoliceBar.margin
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 1.2
        spacing:             PoliceBar.margin

        // A plain Button paints the style's own opaque background, which reads as a white slab
        // on this dark bar.
        Button {
            Layout.preferredWidth: bar.menuButtonWidth
            Layout.minimumWidth:   Layout.preferredWidth
            Layout.fillHeight:     true
            leftPadding:           0
            rightPadding:          0
            onClicked:             bar.menuRequested()

            background: Rectangle {
                color: parent.down ? "#33ffffff" : "transparent"
            }

            contentItem: Item {
                QGCColoredImage {
                    anchors.left:           parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width:                  bar.iconSize
                    height:                 bar.iconSize
                    source:                 "qrc:/qmlimages/Hamburger.svg"
                    color:                  "white"
                    fillMode:               Image.PreserveAspectFit
                    sourceSize.height:      height
                }
            }
        }

        Image {
            Layout.preferredHeight: PoliceBar.logoHeight
            Layout.preferredWidth:  Layout.preferredHeight * (1153 / 122)
            Layout.alignment:       Qt.AlignVCenter
            source:                 "/res/DavinciLabsLogo.png"
            fillMode:               Image.PreserveAspectFit
            smooth:                 true
        }

        // The first thing read on the bar. A band the full height of the bar rather than a pill:
        // at seven inches in daylight the shape is found before the word is, and the gradient
        // fading to the right lets the band be as wide as the sentence needs without drawing a
        // box that then has to be measured against everything beside it.
        Item {
            id:         statusBanner
            objectName: "policeStatusBanner"

            Layout.fillHeight:     true
            Layout.preferredWidth: bannerRow.implicitWidth + bar._itemGap * 2.4
            Layout.minimumWidth:   Layout.preferredWidth

            Rectangle {
                anchors.fill: parent

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.00; color: Qt.rgba(bar._accent.r, bar._accent.g, bar._accent.b, 0.34) }
                    GradientStop { position: 0.46; color: Qt.rgba(bar._accent.r, bar._accent.g, bar._accent.b, 0.26) }
                    GradientStop { position: 0.84; color: Qt.rgba(bar._accent.r, bar._accent.g, bar._accent.b, 0.05) }
                    GradientStop { position: 1.00; color: Qt.rgba(bar._accent.r, bar._accent.g, bar._accent.b, 0.00) }
                }
            }

            Rectangle {
                anchors.left:           parent.left
                anchors.verticalCenter: parent.verticalCenter
                width:                  Math.max(2, bar.height * 0.03)
                height:                 parent.height * 0.66
                color:                  bar._accent
            }

            Row {
                id:                     bannerRow
                anchors.left:           parent.left
                anchors.leftMargin:     bar._itemGap
                anchors.verticalCenter: parent.verticalCenter
                spacing:                bar._innerGap

                BarText {
                    anchors.verticalCenter: parent.verticalCenter
                    color:                  bar._accent
                    font.weight:            Font.DemiBold
                    font.pixelSize:         bar.valueSize
                    text:                   bar.status ? bar.status.text : ""
                }

                // Only where there is something to go and read: an arming refusal. A chevron on
                // every state would say the drawer holds an answer when all it holds is the log.
                QGCColoredImage {
                    objectName:             "policeStatusChevron"
                    anchors.verticalCenter: parent.verticalCenter
                    width:                  bar.iconSize * 0.62
                    height:                 width
                    sourceSize.height:      height
                    source:                 "qrc:/InstrumentValueIcons/arrow-simple-down.svg"
                    color:                  bar._accent
                    fillMode:               Image.PreserveAspectFit
                    opacity:                0.85
                    visible:                bar.status && bar.status.blocked === true
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled:      bar.vehicle
                onClicked:    mainWindow.showIndicatorDrawer(statusPage, statusBanner)
            }
        }

        // The aircraft's own sentences - prearm refusals, EKF and thrust warnings. Beside the
        // banner because almost every one of them explains why the banner is not green.
        Item {
            id:         messageItem
            objectName: "policeMessageItem"

            Layout.alignment:       Qt.AlignVCenter
            Layout.preferredWidth:  Math.max(bar.iconSize, ScreenTools.minTouchPixels)
            Layout.minimumWidth:    Layout.preferredWidth
            Layout.preferredHeight: bar._touchHeight
            // Always there once a vehicle is: opening the drawer clears the unread count, and a
            // pictogram that vanishes on the tap that read it leaves no way back to the list.
            visible:                bar.vehicle

            QGCColoredImage {
                id:                messageIcon
                anchors.centerIn:  parent
                width:             bar.iconSize
                height:            bar.iconSize
                source:            "qrc:/InstrumentValueIcons/chat-bubble-dots.svg"
                fillMode:          Image.PreserveAspectFit
                // Both dimensions: this glyph comes out blank when the provider has to derive
                // its width.
                sourceSize.width:  bar.iconSize
                sourceSize.height: bar.iconSize
                color:             !bar.vehicle                     ? bar.idleColor
                                   : bar.vehicle.messageTypeError   ? bar.alarmColor
                                   : bar.vehicle.messageTypeWarning ? bar.warnColor
                                                                    : "white"
            }

            Rectangle {
                anchors.right:       messageIcon.right
                anchors.top:         messageIcon.top
                anchors.rightMargin: -bar.badgeSize * 0.4
                anchors.topMargin:   -bar.badgeSize * 0.4
                width:               Math.max(height, badgeText.implicitWidth + bar.badgeSize * 0.7)
                height:              bar.badgeSize * 1.6
                radius:              height / 2
                color:               bar.alarmColor
                visible:             bar.vehicle && (bar.vehicle.messageCount > 0)

                BarText {
                    id:               badgeText
                    anchors.centerIn: parent
                    color:            "white"
                    font.weight:      Font.DemiBold
                    font.pixelSize:   bar.badgeSize
                    text:             bar.vehicle ? bar.vehicle.messageCount : ""
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked:    mainWindow.showIndicatorDrawer(bar.messagesPage, messageItem)
            }
        }

        // ------------------------------------------------------------- the right cluster
        //
        // Takes the whole middle so the cluster can hang off the right edge, and clips so a bar
        // too narrow for the row loses its left end - the flight mode - rather than drawing over
        // the banner.
        Item {
            id:                    rightRegion
            clip:                  true
            Layout.fillWidth:      true
            Layout.preferredWidth: 0
            Layout.fillHeight:     true

            RowLayout {
                anchors.right:          parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing:                bar._itemGap

                // What the aircraft is doing, and where it is changed. The tap is taken by a
                // stock FlightModeIndicator laid over this item rather than by a MouseArea of
                // our own: the mode list it drops is an inline Component inside that file, so
                // the only way to open the one the rest of QGC opens - on PX4 and on ArduPilot
                // alike - is to let the stock indicator's own MouseArea take the press. It is
                // drawn at zero opacity and not hidden, since a hidden item takes no input, and
                // it is anchored to this item so the drawer it positions lands under what the
                // operator actually tapped.
                Item {
                    id:         flightModeItem
                    objectName: "policeFlightModeItem"

                    Layout.alignment:       Qt.AlignVCenter
                    Layout.preferredWidth:  flightModeRow.implicitWidth
                    Layout.preferredHeight: bar._touchHeight
                    visible:                bar.vehicle

                    Row {
                        id:               flightModeRow
                        anchors.centerIn: parent
                        spacing:          bar._innerGap

                        BarIcon {
                            source: "/qmlimages/Quad.svg"
                        }

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            width:                  Math.min(implicitWidth, ScreenTools.defaultFontPixelWidth * 12)
                            elide:                  Text.ElideRight
                            color:                  "white"
                            font.weight:            Font.DemiBold
                            font.pixelSize:         bar.valueSize
                            text:                   bar.vehicle ? bar.vehicle.flightMode : ""
                        }
                    }

                    // The stock indicator's own MouseArea is anchored to the stock row, not to
                    // the indicator, so it comes out wider than this item - the stock label is
                    // set at a larger size than ours. Holding it in an item that clips keeps the
                    // tap on this item: a clipping item drops pointer events that fall outside
                    // it, so the gap and the satellites beside us stay theirs. Our own row is
                    // outside this item and so is not clipped.
                    Item {
                        anchors.fill: parent
                        clip:         true

                        FlightModeIndicator {
                            anchors.fill: parent
                            opacity:      0
                        }
                    }
                }

                // Satellites and the fix behind them. No signal bars: bars are the idiom for
                // radio strength, and eighteen satellites with no fix would have drawn four.
                Item {
                    id:         gpsItem
                    objectName: "policeGpsItem"

                    Layout.alignment:       Qt.AlignVCenter
                    Layout.preferredWidth:  gpsRow.implicitWidth
                    Layout.preferredHeight: bar._touchHeight
                    visible:                bar.vehicle

                    Row {
                        id:               gpsRow
                        anchors.centerIn: parent
                        spacing:          bar._innerGap

                        BarIcon {
                            source: "/qmlimages/Gps.svg"
                            tint:   bar.gpsFixed ? "white" : bar.warnColor
                        }

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  bar.gpsFixed ? "white" : bar.warnColor
                            font.weight:            Font.DemiBold
                            font.pixelSize:         bar.valueSize
                            text:                   bar.vehicle ? bar.vehicle.gps.count.valueString : ""
                        }

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  bar.gpsFixed ? "white" : bar.warnColor
                            font.pixelSize:         bar.labelSize
                            text:                   bar.gpsFixText
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    mainWindow.showIndicatorDrawer(bar.gpsPage, gpsItem)
                    }
                }

                // The pilot's radio, and the way a link is attached and dropped: with no aircraft
                // this opens the link chooser, with one it lists the links that are up. Bars here
                // because this one really is a signal strength - empty and silent when the
                // aircraft is connected but reports no RSSI, since a level invented for an
                // airframe that never sent one is the reading that gets believed.
                Item {
                    id:         rfItem
                    objectName: "policeRfItem"

                    readonly property bool _lost: bar.vehicle && !bar.linkUp
                    readonly property color _tint: _lost ? bar.alarmColor
                                                   : (bar.rcAvailable && (bar.rcLevel <= 1)) ? bar.alarmColor
                                                                                             : "white"
                    readonly property string _text: !bar.vehicle ? qsTr("연결 안 됨")
                                                    : _lost      ? qsTr("신호 끊김")
                                                                 : ""

                    Layout.alignment:       Qt.AlignVCenter
                    Layout.preferredWidth:  rfRow.implicitWidth
                    Layout.preferredHeight: bar._touchHeight

                    Row {
                        id:               rfRow
                        anchors.centerIn: parent
                        spacing:          bar._innerGap

                        BarIcon {
                            source: "/qmlimages/RC.svg"
                            tint:   rfItem._tint
                        }

                        BarGauge {
                            anchors.verticalCenter: parent.verticalCenter
                            // Empty with no aircraft and on a lost link: whatever was last heard
                            // is not what the radio is doing now.
                            level:                  (bar.vehicle && bar.linkUp && bar.rcAvailable) ? bar.rcLevel : 0
                            tint:                   rfItem._tint
                        }

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  rfItem._tint
                            font.weight:            Font.DemiBold
                            font.pixelSize:         bar.valueSize
                            text:                   rfItem._text
                            visible:                text !== ""
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    mainWindow.showIndicatorDrawer(bar.vehicle ? bar.linkConnectedPage
                                                                                 : bar.linkSelectPage,
                                                                     rfItem)
                    }
                }

                // The only number on the bar that changes an operator's plan.
                Item {
                    id:         batteryItem
                    objectName: "policeBatteryItem"

                    Layout.alignment:       Qt.AlignVCenter
                    Layout.preferredWidth:  batteryRow.implicitWidth
                    Layout.preferredHeight: bar._touchHeight
                    visible:                bar.lowestBattery

                    Row {
                        id:               batteryRow
                        anchors.centerIn: parent
                        spacing:          bar._innerGap

                        BatteryGlyph {
                            tint:    bar.batteryColor
                            percent: bar.batteryPercent
                        }

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  bar.batteryColor
                            font.weight:            Font.DemiBold
                            font.pixelSize:         bar.valueSize
                            // Percentage when the pack reports one, its voltage when it does not.
                            // Bindings run whether or not the group is visible, so the pack is
                            // checked here too rather than only in the group's visibility.
                            text:                   !bar.lowestBattery
                                                        ? ""
                                                        : (isNaN(bar.batteryPercent)
                                                               ? bar.lowestBattery.voltage.valueString + qsTr(" V")
                                                               : qsTr("%1 %").arg(Math.round(bar.batteryPercent)))
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    mainWindow.showIndicatorDrawer(batteryPage, batteryItem)
                    }
                }

                // The aircraft's own wind estimate, which costs no network and describes the air
                // the aircraft is actually in. Speed alone: the bearing convention is not the
                // same on the two firmwares (ArduPilot's WIND is the direction the wind comes
                // from, PX4's WIND_COV carries the opposite sense into the same Fact), and a
                // bearing read the wrong way round is the difference between planning the return
                // into the wind and planning it downwind. telemetryAvailable stays false until an
                // estimate arrives, so an airframe that does not estimate wind shows nothing here
                // rather than a reading of zero it never made.
                Row {
                    objectName:       "policeWindItem"
                    Layout.alignment: Qt.AlignVCenter
                    spacing:          bar._innerGap
                    visible:          bar.vehicle && bar.vehicle.wind.telemetryAvailable

                    BarText {
                        anchors.verticalCenter: parent.verticalCenter
                        color:                  "white"
                        font.weight:            Font.DemiBold
                        font.pixelSize:         bar.valueSize
                        text:                   bar.vehicle
                                                    ? bar.vehicle.wind.speed.valueString + " " +
                                                      bar.vehicle.wind.speed.units
                                                    : ""
                    }
                }

                // The one rule on the bar: everything left of it is the aircraft, the date and
                // time right of it is the station's own.
                BarSep {}

                // Procurement wants the flight's date readable off the video screen, so the day
                // is on the bar and not only the time. Dim, and the same size and weight as the
                // values: it is read once and then ignored.
                BarText {
                    id:               barClock
                    objectName:       "policeDateTime"
                    Layout.alignment: Qt.AlignVCenter
                    color:            bar.labelColor
                    font.weight:      Font.DemiBold
                    font.pixelSize:   bar.valueSize
                    text:             Qt.formatDateTime(new Date(), "MM-dd HH:mm:ss")

                    Timer {
                        interval:    1000
                        running:     true
                        repeat:      true
                        onTriggered: barClock.text = Qt.formatDateTime(new Date(), "MM-dd HH:mm:ss")
                    }
                }
            }
        }
    }

    // The two drawers this bar brings with it. The other four are the dashboard's and are
    // handed in as properties.
    Component {
        id: statusPage

        PoliceStatusPage {
            headingText: bar.status ? bar.status.text : ""
            armBlocked:  bar.status ? (bar.status.blocked === true) : false
        }
    }

    Component {
        id: batteryPage

        PoliceBatteryPage {
            battery: bar.lowestBattery
        }
    }
}
