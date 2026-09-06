import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.FlightMap
import QGroundControl.FlyView
import QGroundControl.SiyiCamera
import QGroundControl.Toolbar

Item {
    id: root

    required property var guidedController

    // The controller reports the box centre normalised 0..1; the overlay wants the top-left
    // corner in the module's reference frame, so convert here.
    readonly property bool aiTargetVisible: App.SiyiAiController.hasTarget && !App.SiyiAiController.targetLost
    readonly property real aiTargetWidth:   App.SiyiAiController.targetWidth * _aiRefWidth
    readonly property real aiTargetHeight:  App.SiyiAiController.targetHeight * _aiRefHeight
    readonly property real aiTargetX:       App.SiyiAiController.targetCentreX * _aiRefWidth - aiTargetWidth / 2
    readonly property real aiTargetY:       App.SiyiAiController.targetCentreY * _aiRefHeight - aiTargetHeight / 2
    readonly property string aiTargetLabel: App.SiyiAiController.targetTypeName

    readonly property real _aiRefWidth:  1280
    readonly property real _aiRefHeight: 720

    // One line the operator can read off: what the module is tracking, where its box centre
    // sits in the module's 1280×720 frame, how big it is, and the laser range if the pod has
    // one. Empty when nothing is tracked, so the panels can key visibility on it.
    readonly property string aiTargetInfo: aiTargetVisible
        ? qsTr("%1 · 위치 (%2, %3) · 크기 %4×%5 px%6")
              .arg(aiTargetLabel)
              .arg(Math.round(App.SiyiAiController.targetCentreX * _aiRefWidth))
              .arg(Math.round(App.SiyiAiController.targetCentreY * _aiRefHeight))
              .arg(Math.round(aiTargetWidth))
              .arg(Math.round(aiTargetHeight))
              .arg(App.SiyiCameraController.rangefinderAvailable
                   ? qsTr(" · LRF %1 m").arg(Number(App.SiyiCameraController.rangefinderDistance).toFixed(1))
                   : "")
        : ""

    /// Target picking only makes sense once the module is up and recognising.
    readonly property bool _aiPickEnabled: App.SiyiAiController.connected && App.SiyiAiController.recognitionEnabled

    // ---------------------------------------------------------------- RTL return altitude
    //
    // Return altitude is a firmware parameter, not an argument of the RTL command, so the
    // chosen height is written first and the vehicle is then told to return. Parameter name
    // and unit differ per firmware, hence the lookup rather than a hardcoded name.

    FactPanelController { id: rtlParamController }

    readonly property var _rtlAltCandidates: [
        { name: "RTL_RETURN_ALT", scale: 1 },     // PX4, metres
        { name: "RTL_ALT_M",      scale: 1 },     // ArduPilot, metres
        { name: "RTL_ALT",        scale: 100 },   // ArduPilot legacy, centimetres
        { name: "ALT_HOLD_RTL",   scale: 100 }    // ArduPlane, centimetres
    ]

    function _rtlAltFact() {
        for (let i = 0; i < _rtlAltCandidates.length; ++i) {
            const c = _rtlAltCandidates[i]
            if (rtlParamController.parameterExists(-1, c.name)) {
                return { fact: rtlParamController.getParameterFact(-1, c.name, false), scale: c.scale }
            }
        }
        return null
    }

    function _returnAt(altitudeMetres) {
        if (altitudeMetres > 0) {
            const found = _rtlAltFact()
            if (found && found.fact) {
                found.fact.rawValue = altitudeMetres * found.scale
            } else {
                trackToast.text = qsTr("복귀 고도 파라미터를 찾지 못해 기체 설정값으로 복귀합니다")
                trackToast.show()
            }
        }
        guidedController.confirmAction(guidedController.actionRTL)
    }

    // The module rejects selections that arrive in the wrong mode or on an unsupported
    // stream; without this the operator would just see nothing happen.
    Connections {
        target: App.SiyiAiController
        function onTrackRequestFailed(reason) {
            trackToast.text = reason
            trackToast.show()
        }
    }

    Rectangle {
        id:      trackToast
        property alias text: toastLabel.text

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom:           parent.bottom
        anchors.bottomMargin:     root._bottomInset + 12
        width:   toastLabel.implicitWidth + 28
        height:  toastLabel.implicitHeight + 16
        radius:  4
        color:   "#e6b00000"
        opacity: 0
        visible: opacity > 0
        z:       30

        function show() { toastAnim.restart() }

        Text {
            id:               toastLabel
            anchors.centerIn: parent
            color:            "white"
            font.pixelSize:   Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
        }

        SequentialAnimation {
            id: toastAnim
            NumberAnimation { target: trackToast; property: "opacity"; to: 1; duration: 120 }
            PauseAnimation  { duration: 2600 }
            NumberAnimation { target: trackToast; property: "opacity"; to: 0; duration: 400 }
        }
    }

    property string expandedPanel
    /// The fly view's map item. Mirrored into the top-right window while a camera is
    /// full screen, so the operator keeps the aircraft's position in view.
    property var mapItem: null

    readonly property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property var _battery:       _activeVehicle && _activeVehicle.batteries.count > 0 ? _activeVehicle.batteries.get(0) : null

    QGCPalette { id: qgcPal }

    // Up means a vehicle is connected and its link is alive: the plug indicator on the
    // top bar's right end reads this.
    readonly property bool _linkUp: _activeVehicle ? !_communicationLost : false
    readonly property bool _communicationLost: _activeVehicle
                                               ? _activeVehicle.vehicleLinkManager.communicationLost
                                               : false

    // ------------------------------------------------------------------ link loss warning
    //
    // The autopilot reports the receiver through the SYS_STATUS sensor bits: present says the
    // airframe has one at all, unhealthy says it is currently not receiving. Checking present
    // first keeps an airframe flown without RC from warning permanently.
    // MAV_SYS_STATUS_SENSOR_RC_RECEIVER. The generated MAVLinkEnums namespace exposes the
    // MAV_SYS_STATUS_SENSOR type but not this member, so the symbolic form resolves to
    // undefined; the protocol value is fixed by the MAVLink spec.
    readonly property int _rcSensorBit: 0x10000
    readonly property bool _rcLinkLost: _activeVehicle
                                        && (_activeVehicle.sensorsPresentBits & _rcSensorBit)
                                        && (_activeVehicle.sensorsUnhealthyBits & _rcSensorBit)
    readonly property real _touchHeight:  Math.max(ScreenTools.minTouchPixels * 1.4, ScreenTools.defaultFontPixelHeight * 2.1)
    // QGCToolBarButton's icon height: the ☰ in the Plan and Configuration toolbars is drawn
    // at this size, and the menu button should be one size everywhere.
    readonly property real _menuIconSize: ScreenTools.defaultFontPixelHeight * 1.2
    // Sized to its own pictograms now that QGC's toolbar indicators are not in it: the row
    // needs a touch target's height and nothing more, and every pixel saved here goes to the
    // map and the camera windows.
    readonly property real _statusHeight: Math.max(ScreenTools.minTouchPixels,
                                                   ScreenTools.defaultFontPixelHeight * 2.6,
                                                   _barIconSize * 1.8)
    readonly property color _panelColor:  "#e5121b24"
    /// Gap kept clear along the bottom edge now that the control panel floats rather than
    /// occupying two full-width bars.
    readonly property real _bottomInset:  8
    readonly property color _accentColor: "#33c7ff"
    // Status palette. Grey rests, white is fine, red is the only alarm. Painting every
    // state a different bright colour is what makes a bar unreadable at a glance.
    // One rhythm for the top bar: pictograms at one size, one label size, one value size.
    readonly property real  _barIconSize: Math.max(20, ScreenTools.defaultFontPixelHeight * 1.15)
    readonly property real  _labelSize:   Math.max(11, ScreenTools.defaultFontPixelHeight * 0.62)
    readonly property real  _valueSize:   Math.max(12, ScreenTools.defaultFontPixelHeight * 0.78)
    readonly property color _labelColor:  "#9fb2c4"
    readonly property color _barColor:    "#0c1218"
    readonly property color _readyColor:  "#22c46a"
    readonly property color _warnColor:   "#ffb020"

    // Bars, not numbers, for the radios: four steps is all an operator acts on, and the step
    // is readable at arm's length in a way that a dBm figure is not.
    readonly property int _gpsLevel: {
        if (!_activeVehicle) {
            return 0
        }
        const lock = _activeVehicle.gps.lock.rawValue
        if (lock < 2) {
            return 0
        }
        if (lock < 3) {
            return 1
        }
        const sats = _activeVehicle.gps.count.rawValue
        return sats >= 16 ? 4 : sats >= 12 ? 3 : sats >= 8 ? 2 : 1
    }

    readonly property bool _rcAvailable: _activeVehicle && _activeVehicle.rcRSSI.rawValue > 0 &&
                                         _activeVehicle.rcRSSI.rawValue <= 100
    readonly property int  _rcLevel:     _rcAvailable
                                             ? Math.max(1, Math.ceil(_activeVehicle.rcRSSI.rawValue / 25)) : 0

    // Only a SiK radio's RADIO_STATUS is converted to dBm; everything else forwards the raw
    // 0..254 field, where a weak 20 would read as a strong -20 and paint four bars on a link
    // about to drop. Out of that range the group simply does not appear.
    readonly property bool _telemAvailable: _activeVehicle &&
                                            (_activeVehicle.radioStatus.lrssi.rawValue < 0) &&
                                            (_activeVehicle.radioStatus.lrssi.rawValue >= -120)
    readonly property int  _telemLevel: {
        if (!_telemAvailable) {
            return 0
        }
        const dbm = _activeVehicle.radioStatus.lrssi.rawValue
        return dbm >= -70 ? 4 : dbm >= -85 ? 3 : dbm >= -95 ? 2 : 1
    }

    // The worst pack, not the first: an airframe can report several, and the flight pack is
    // not always instance 0.
    readonly property var _lowestBattery: {
        if (!_activeVehicle) {
            return null
        }
        let worst = null
        for (let i = 0; i < _activeVehicle.batteries.count; ++i) {
            const battery = _activeVehicle.batteries.get(i)
            const percent = battery.percentRemaining.rawValue
            if (isNaN(percent)) {
                continue
            }
            if (!worst || (percent < worst.percentRemaining.rawValue)) {
                worst = battery
            }
        }
        // Nothing reports a percentage on a good many ArduPilot setups; the first pack's
        // voltage is still worth showing.
        return worst ? worst : (_activeVehicle.batteries.count > 0 ? _activeVehicle.batteries.get(0) : null)
    }

    readonly property real _batteryPercent: _lowestBattery ? _lowestBattery.percentRemaining.rawValue : NaN

    // MAV_BATTERY_CHARGE_STATE: 2 LOW, 3 CRITICAL, 4 EMERGENCY, 5 FAILED, 6 UNHEALTHY. The
    // autopilot's own judgement of the pack, which is what a failsafe acts on — the battery
    // indicator's thresholds are display bands (80 / 60 by default) and mean nothing here.
    readonly property int _batteryState: _lowestBattery ? _lowestBattery.chargeState.rawValue : 0
    readonly property bool _batteryLow:      (_batteryState === 2) || (!isNaN(_batteryPercent) && _batteryPercent <= 30)
    readonly property bool _batteryCritical: (_batteryState >= 3) || (!isNaN(_batteryPercent) && _batteryPercent <= 20)

    readonly property color _batteryColor: {
        if (!_lowestBattery) {
            return _idleColor
        }
        if (_batteryCritical) {
            return _alarmColor
        }
        return _batteryLow ? _warnColor : "white"
    }

    /// The one line of prose on the bar: the most urgent thing wrong, or nothing at all.
    readonly property string _warningText: {
        if (!_activeVehicle) {
            return ""
        }
        if (_communicationLost) {
            return qsTr("통신 두절 — 기체 응답 없음")
        }
        if (_rcLinkLost) {
            return qsTr("조종기 신호 끊김")
        }
        if (_batteryCritical) {
            return qsTr("배터리 위급 — 즉시 복귀하십시오")
        }
        if (!_activeVehicle.allSensorsHealthy) {
            return qsTr("센서 이상 — 기체 상태를 확인하십시오")
        }
        // Why arming is refused used to be readable on the stock status indicator, which this
        // bar replaced; without it the operator is left with a button that does nothing.
        if (_activeVehicle.prearmError.length > 0) {
            return _activeVehicle.prearmError
        }
        if (_batteryLow) {
            return qsTr("배터리 낮음 — 복귀를 준비하십시오")
        }
        return ""
    }

    readonly property color _idleColor:   "#8a9199"
    readonly property color _normalColor: "#e8edf2"
    readonly property color _alarmColor:  "#ff5b5b"

    component BarIcon: QGCColoredImage {
        property color tint: "white"

        anchors.verticalCenter: parent.verticalCenter
        width:                  root._barIconSize
        height:                 root._barIconSize
        color:                  tint
        fillMode:               Image.PreserveAspectFit
        sourceSize.height:      root._barIconSize
    }

    // Four steps of signal, the filled ones in the group's colour and the rest ghosted.
    component BarGauge: Row {
        property int   level: 0
        property color tint:  "white"

        // Explicit, so the bars can hang from the bottom without sizing their own parent.
        height:  root._barIconSize
        spacing: Math.max(2, root._barIconSize * 0.09)

        Repeater {
            model: 4

            Rectangle {
                required property int index
                anchors.bottom: parent.bottom
                width:          Math.max(3, root._barIconSize * 0.16)
                height:         root._barIconSize * (0.3 + index * 0.19)
                radius:         1
                color:          index < parent.level ? parent.tint : "#38ffffff"
            }
        }
    }

    component BarSep: Rectangle {
        Layout.alignment:       Qt.AlignVCenter
        Layout.preferredWidth:  1
        Layout.preferredHeight: root._barIconSize * 1.2
        color:                  "#26ffffff"
    }

    signal menuRequested()

    // Camera window sizing and one-time docking along the bottom edge. Windows keep user
    // positions afterwards; a resize only re-clamps them through the drag axis limits.
    /// Which pod sensor the EO window shows. The pod's sub stream stays thermal either way,
    /// so the IR window is unaffected by this choice.
    property bool eoShowsWideAngle: false

    /// The AI module's own RTSP feed replaces the main window when it is enabled - the same
    /// picture with the module's boxes drawn in. The thermal window always keeps the sub stream.
    readonly property bool _aiStreamActive:
        QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue &&
        QGroundControl.settingsManager.siyiCameraSettings.aiRtspUrl.rawValue !== ""

    /// Put the pod into the three-sensor layout as soon as it answers, so the operator does
    /// not have to press a button to get all three windows populated. Re-applied on every
    /// reconnect because the pod keeps its own last mode across power cycles.
    Connections {
        target: App.SiyiCameraController
        function onConnectedChanged() {
            if (App.SiyiCameraController.connected) {
                root._applyPodStreams()
            }
        }
    }

    // The pod carries one sensor per stream: main to the EO window, sub to the IR window.
    // Image mode 3 is zoom on main, 5 is wide angle on main, and both leave thermal on sub.
    // The AI module infers on the main stream and its manual requires the zoom camera, so
    // enabling AI forces the zoom choice.
    function _applyPodStreams() {
        const wide = eoShowsWideAngle &&
                     !QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue
        App.SiyiCameraController.setCameraImageType(wide ? 5 : 3)
        eoShowsWideAngle = wide
    }

    // Re-apply when AI is switched on or off, since AI pins the main stream to the zoom
    // camera and the operator's wide-angle choice has to give way.
    Connections {
        target: QGroundControl.settingsManager.siyiCameraSettings.aiEnabled
        function onRawValueChanged() {
            if (App.SiyiCameraController.connected) {
                root._applyPodStreams()
            }
        }
    }

    readonly property real _gripHeight: Math.max(ScreenTools.minTouchPixels * 0.7, ScreenTools.defaultFontPixelHeight * 1.2)

    /// The FPV window only exists once an address is set, so window count is not fixed.
    readonly property bool _fpvConfigured:
        QGroundControl.settingsManager.siyiCameraSettings.fpvRtspUrl.rawValue.trim() !== ""

    /// One window per sensor: the FPV camera, the pod's main stream and the pod's thermal.
    readonly property int _windowCount: 3

    // The windows stack down the right edge, so the width driving their 16:9 bodies is bounded
    // by the height left between the top bar and the control panel below them — sized on width
    // alone the last window would run off the bottom.
    readonly property real _windowWidth: {
        // The three windows fill the whole right column, top bar to bottom edge, flush and
        // borderless. Height drives width at 16:9, so they stack edge to edge with no gap
        // above or below. No width cap: filling the column top-to-bottom is what was asked.
        const avail = height - topBar.height
        return avail * 16 / (9 * _windowCount)
    }

    property bool _userMovedWindows: false

    function _dockWindows() {
        const windows = [primaryWindow, secondaryWindow, sharedWindow]
        const xDock = width - _windowWidth
        let y = topBar.height
        for (let i = 0; i < windows.length; ++i) {
            windows[i].x = xDock
            windows[i].y = y
            y += windows[i].height
        }
    }

    // Re-dock until the operator moves a window; afterwards positions are theirs. Both
    // triggers matter: the bar settles vertically after load, and the window width used for
    // the x spacing follows the root width.
    function _redockIfPristine() {
        if (!_userMovedWindows && height > 200) {
            _dockWindows()
        }
    }

    onWidthChanged: _redockIfPristine()
    onHeightChanged: _redockIfPristine()
    Component.onCompleted: {
        _redockIfPristine()
        if (App.SiyiCameraController.connected) {
            _applyPodStreams()
        }
    }

    Connections {
        target: root
        function onHeightChanged() { root._redockIfPristine() }
    }

    // ------------------------------------------------------------------- flight log
    //
    // Procurement requires the operator to read the flight's date, duration and distance from
    // the video screen in flight. Duration and distance are Vehicle facts; the wall-clock
    // takeoff instant is not tracked anywhere, so it is captured here on the arm transition.
    //
    // Deriving it from flightTime alone would jitter: that fact ticks once a second, so
    // now - flightTime lands on a different second each update.
    // Signal handlers, not an on<Prop>Changed on a readonly property: QML rejects those and
    // the whole component then fails to load, taking the Fly view with it.
    property var _takeoffTime: null

    Connections {
        target: root._activeVehicle
        ignoreUnknownSignals: true

        function onArmedChanged(armed) {
            if (armed) {
                root._takeoffTime = new Date()
            }
            // Kept after disarm so the completed flight stays readable on the ground; the
            // next arm overwrites it.
        }
    }

    // Attaching to a vehicle already in the air misses the arm transition. flightTime still
    // carries the elapsed seconds, which places the takeoff to the second.
    Connections {
        target: QGroundControl.multiVehicleManager

        function onActiveVehicleChanged(vehicle) {
            if (vehicle && vehicle.armed) {
                const elapsed = vehicle.getFact("flightTime")
                root._takeoffTime = new Date(Date.now() - (elapsed ? elapsed.rawValue : 0) * 1000)
            } else {
                root._takeoffTime = null
            }
        }
    }

    readonly property string _takeoffText:
        _takeoffTime ? Qt.formatDateTime(_takeoffTime, "MM-dd HH:mm:ss") : qsTr("이륙 전")

    /// One AI switch: the on-device detector and the pod module's own recognition follow it,
    /// so the operator arms one thing before a long press can pick a target.
    function _setAiEnabled(on) {
        App.PersonDetector.enabled = on
        if (App.SiyiAiController.connected) {
            App.SiyiAiController.setRecognition(on)
        }
    }

    // A module that was absent when the switch was flipped never heard the command.
    Connections {
        target: App.SiyiAiController

        function onConnectedChanged() {
            // Only when the module disagrees: the pod's own panel can switch recognition too,
            // and a reconnect should not quietly undo what was set there.
            if (App.SiyiAiController.connected &&
                    App.SiyiAiController.recognitionEnabled !== App.PersonDetector.enabled) {
                App.SiyiAiController.setRecognition(App.PersonDetector.enabled)
            }
        }
    }

    function _toggleExpanded(panelName) {
        expandedPanel = expandedPanel === panelName ? "" : panelName
        if (expandedPanel.length > 0) {
            forceActiveFocus()
        }
    }

    focus: expandedPanel.length > 0
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (expandedPanel.length > 0 && (event.key === Qt.Key_Back || event.key === Qt.Key_Escape)) {
            expandedPanel = ""
            event.accepted = true
        }
    }

    // ------------------------------------------------------------------------- top bar
    //
    // Read at a glance, never touched: pictograms with a signal gauge rather than words, in
    // the order a pilot checks them — what the aircraft is doing, then what it is flying on
    // (satellites, radio, telemetry, battery), then the pod's AI, then the link. Words are
    // spent only where a picture cannot carry the value: the flight mode, the battery
    // percentage, and the warning line on the left, which is empty when nothing is wrong.
    // Colour means something is wrong; everything healthy is plain white.
    Rectangle {
        id: topBar
        anchors.left:  parent.left
        anchors.right: parent.right
        anchors.top:   parent.top
        height:        root._statusHeight
        color:         root._barColor
        z:             4

        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill:        parent
            anchors.leftMargin:  ScreenTools.defaultFontPixelWidth * 0.4
            anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 1.2
            spacing:             ScreenTools.defaultFontPixelWidth * 1.1

            // A plain Button paints the style's own opaque background, which read as a white
            // slab on this dark bar. Transparent background plus an explicitly light icon
            // matches how the toolbars in the other views render theirs.
            Button {
                Layout.preferredWidth:  root._menuIconSize + ScreenTools.defaultFontPixelWidth * 2
                Layout.fillHeight:      true
                onClicked:              root.menuRequested()

                background: Rectangle {
                    color: parent.down ? "#33ffffff" : "transparent"
                }

                contentItem: Item {
                    QGCColoredImage {
                        anchors.centerIn:  parent
                        width:             root._menuIconSize
                        height:            root._menuIconSize
                        source:            "qrc:/qmlimages/Hamburger.svg"
                        color:             "white"
                        fillMode:          Image.PreserveAspectFit
                        sourceSize.height: height
                    }
                }
            }

            // The police layout replaces FlyViewToolBar, so the brand mark lives here.
            Image {
                Layout.preferredHeight: root._statusHeight * 0.36
                Layout.preferredWidth:  Layout.preferredHeight * (1153 / 122)
                Layout.alignment:       Qt.AlignVCenter
                source:                 "/res/DavinciLabsLogo.png"
                fillMode:               Image.PreserveAspectFit
                smooth:                 true
            }

            // The one line of prose on the bar, and the reason the middle is kept empty:
            // when something is wrong it appears here, where nothing else ever draws.
            Text {
                Layout.fillWidth:       true
                Layout.leftMargin:      ScreenTools.defaultFontPixelWidth
                Layout.alignment:       Qt.AlignVCenter
                color:                  root._alarmColor
                font.bold:              true
                font.pixelSize:         root._valueSize
                elide:                  Text.ElideRight
                text:                   root._warningText
            }

            // Required on the video screen for delivery: when this flight began and how many
            // times this airframe has flown. Duration and distance are on the telemetry bar
            // bottom left, so they are not repeated here.
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing:          ScreenTools.defaultFontPixelWidth * 1.1
                // Gives its space up the moment there is something wrong to read.
                visible:          root._activeVehicle && (root._warningText.length === 0) &&
                                  (root.width > ScreenTools.defaultFontPixelWidth * 95)

                Repeater {
                    model: [
                        { label: qsTr("이륙"),   value: root._takeoffText },
                        { label: qsTr("이륙 횟수"), value: qsTr("%1회").arg(App.TakeoffCounter.takeoffCount) }
                    ]

                    delegate: Row {
                        id: logItem
                        required property var modelData
                        anchors.verticalCenter: parent.verticalCenter
                        spacing:                5

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  root._labelColor
                            font.pixelSize:         root._labelSize
                            text:                   logItem.modelData.label
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  "white"
                            font.pixelSize:         root._valueSize
                            // Monospace keeps the clock and counters from shifting the row
                            // sideways as digits change.
                            font.family:            "Consolas, monospace"
                            text:                   logItem.modelData.value
                        }
                    }
                }
            }

            BarSep { visible: root._activeVehicle }

            // What the aircraft is doing. The airframe glyph turns green while armed, which is
            // the one state worth a colour of its own.
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing:          ScreenTools.defaultFontPixelWidth * 0.6
                visible:          root._activeVehicle

                BarIcon {
                    source: "/qmlimages/Quad.svg"
                    tint:   (root._activeVehicle && root._activeVehicle.armed) ? root._readyColor : "white"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width:                  Math.min(implicitWidth, ScreenTools.defaultFontPixelWidth * 12)
                    elide:                  Text.ElideRight
                    color:                  "white"
                    font.bold:              true
                    font.pixelSize:         root._valueSize
                    text:                   root._activeVehicle ? root._activeVehicle.flightMode : ""
                }
            }

            BarSep { visible: root._activeVehicle }

            // Satellites: the count is the number an operator quotes, the gauge is the fix
            // quality behind it. A tap opens QGC's own GPS page, which this bar replaces.
            Item {
                Layout.alignment:       Qt.AlignVCenter
                Layout.preferredWidth:  gpsRow.implicitWidth
                Layout.preferredHeight: root._barIconSize * 1.6
                visible:                root._activeVehicle

                Row {
                    id:               gpsRow
                    anchors.centerIn: parent
                    spacing:          ScreenTools.defaultFontPixelWidth * 0.5

                    BarIcon {
                        source: "/qmlimages/Gps.svg"
                        tint:   root._gpsLevel > 1 ? "white" : root._warnColor
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color:                  root._gpsLevel > 1 ? "white" : root._warnColor
                        font.bold:              true
                        font.pixelSize:         root._valueSize
                        text:                   root._activeVehicle
                                                    ? root._activeVehicle.gps.count.valueString : "--"
                    }

                    BarGauge {
                        anchors.verticalCenter: parent.verticalCenter
                        level:                  root._gpsLevel
                        tint:                   root._gpsLevel > 1 ? "white" : root._warnColor
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked:    mainWindow.showIndicatorDrawer(gpsDetailPage, parent)
                }
            }

            // The pilot's radio.
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing:          ScreenTools.defaultFontPixelWidth * 0.5
                visible:          root._rcAvailable

                BarIcon {
                    source: "/qmlimages/RC.svg"
                    tint:   root._rcLevel > 1 ? "white" : root._alarmColor
                }

                BarGauge {
                    anchors.verticalCenter: parent.verticalCenter
                    level:                  root._rcLevel
                    tint:                   root._rcLevel > 1 ? "white" : root._alarmColor
                }
            }

            // The telemetry radio, shown only where one reports its strength.
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing:          ScreenTools.defaultFontPixelWidth * 0.5
                visible:          root._telemAvailable

                BarIcon { source: "/qmlimages/TelemRSSI.svg" }

                BarGauge {
                    anchors.verticalCenter: parent.verticalCenter
                    level:                  root._telemLevel
                    tint:                   root._telemLevel > 1 ? "white" : root._warnColor
                }
            }

            // Battery: the only number on the bar that changes an operator's plan, so it keeps
            // its digits, and the glyph carries the warning colours.
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing:          ScreenTools.defaultFontPixelWidth * 0.5
                visible:          root._lowestBattery

                BarIcon {
                    source: "/qmlimages/Battery.svg"
                    tint:   root._batteryColor
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color:                  root._batteryColor
                    font.bold:              true
                    font.pixelSize:         root._valueSize
                    // Percentage when the pack reports one, its voltage when it does not.
                    text:                   isNaN(root._batteryPercent)
                                                ? root._lowestBattery.voltage.valueString + qsTr(" V")
                                                : qsTr("%1 %").arg(Math.round(root._batteryPercent))
                }
            }

            // STATUSTEXT from the aircraft — prearm refusals, EKF and thrust warnings. The
            // stock status indicator carried this and the police layout hides that toolbar, so
            // without it the messages have nowhere to appear.
            Item {
                Layout.alignment:       Qt.AlignVCenter
                Layout.preferredWidth:  root._barIconSize
                Layout.preferredHeight: root._barIconSize * 1.6
                visible:                root._activeVehicle && (root._activeVehicle.messageCount > 0)

                QGCColoredImage {
                    anchors.centerIn:  parent
                    width:             root._barIconSize
                    height:            root._barIconSize
                    source:            "/res/VehicleMessages.png"
                    fillMode:          Image.PreserveAspectFit
                    sourceSize.height: root._barIconSize
                    color:             !root._activeVehicle          ? root._idleColor
                                       : root._activeVehicle.messageTypeError   ? root._alarmColor
                                       : root._activeVehicle.messageTypeWarning ? root._warnColor
                                                                                : "white"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked:    mainWindow.showIndicatorDrawer(vehicleMessagesPage, parent)
                }
            }

            BarSep {}

            // The pod's tracking module: lit when it is following a target, plain when it is
            // only watching, grey when it is not there.
            Row {
                id:               aiGroup
                Layout.alignment: Qt.AlignVCenter
                spacing:          ScreenTools.defaultFontPixelWidth * 0.5

                readonly property bool tracking: App.SiyiAiController.hasTarget &&
                                                 !App.SiyiAiController.targetLost
                readonly property color tint: !App.SiyiAiController.connected ? root._idleColor
                                              : tracking                     ? root._accentColor
                                                                             : "white"

                BarIcon {
                    source: "/res/police_ai.svg"
                    tint:   aiGroup.tint
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color:                  aiGroup.tint
                    font.bold:              true
                    font.pixelSize:         root._valueSize
                    text: {
                        if (!App.SiyiAiController.connected) {
                            return qsTr("AI 없음")
                        }
                        if (App.SiyiAiController.hasTarget) {
                            return App.SiyiAiController.targetLost ? qsTr("유실") : qsTr("추적중")
                        }
                        return App.SiyiAiController.recognitionEnabled ? qsTr("준비") : qsTr("대기")
                    }
                }
            }

            // The link, at the far right and the only pill on the bar: it is the one thing here
            // that is also a button. Without a vehicle it opens QGC's link chooser; with one it
            // lists the links that are up, each with its own disconnect.
            Rectangle {
                id:                     linkIndicator
                Layout.alignment:       Qt.AlignVCenter
                Layout.preferredHeight: root._barIconSize * 1.65
                Layout.preferredWidth:  linkRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 2.2
                radius:                 height / 2

                // Three states, not two. Nothing attached yet is not the same as a link that
                // dropped mid flight, and only the second one is an alarm.
                readonly property bool  notYet: !root._activeVehicle
                readonly property color tint:   notYet ? root._idleColor
                                                       : (root._linkUp ? root._readyColor : root._alarmColor)

                color: notYet ? "#14ffffff"
                              : (root._linkUp ? "#2622c46a" : "#26ff5b5b")

                Row {
                    id:                     linkRow
                    anchors.centerIn:       parent
                    spacing:                ScreenTools.defaultFontPixelWidth * 0.6

                    // A dot reads as state at any size. A plug drawn at 16 px does not.
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width:                  Math.max(8, root._barIconSize * 0.36)
                        height:                 width
                        radius:                 width / 2
                        color:                  linkIndicator.notYet ? "transparent" : linkIndicator.tint
                        border.width:           linkIndicator.notYet ? Math.max(1, width * 0.16) : 0
                        border.color:           linkIndicator.tint
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color:                  linkIndicator.tint
                        font.bold:              true
                        font.pixelSize:         root._valueSize
                        text:                   linkIndicator.notYet
                                                    ? qsTr("연결 안 됨")
                                                    : (root._linkUp ? qsTr("연결됨") : qsTr("신호 끊김"))
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked:    mainWindow.showIndicatorDrawer(root._activeVehicle ? linkConnectedPage
                                                                                     : linkSelectPage,
                                                                 linkIndicator)
                }
            }
        }
    }

    // QGC's own GPS detail, behind the satellite group.
    Component {
        id: gpsDetailPage

        GPSIndicatorPage {}
    }

    // The aircraft's own messages, behind the message pictogram.
    Component {
        id: vehicleMessagesPage

        ToolIndicatorPage {
            showExpand: false

            contentComponent: Component {
                SettingsGroupLayout {
                    heading: qsTr("기체 메시지")

                    VehicleMessageList {
                        id:      vehicleMessageList
                        visible: !noMessages
                    }

                    QGCLabel {
                        text:    qsTr("새 메시지 없음")
                        visible: vehicleMessageList.noMessages
                    }
                }
            }
        }
    }
    // Pages behind the plug indicator.
    Component {
        id: linkSelectPage

        MainStatusIndicatorOfflinePage {}
    }

    Component {
        id: linkConnectedPage

        ToolIndicatorPage {
            contentComponent: Component {
                SettingsGroupLayout {
                    heading: qsTr("연결된 링크")

                    Repeater {
                        model: QGroundControl.linkManager.linkConfigurations

                        // Auto-connected links (a Pixhawk on USB, say) carry a dynamic
                        // configuration; they are listed too, since they are what the
                        // operator most often wants to drop.
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing:          ScreenTools.defaultFontPixelWidth
                            visible:          !!object.link

                            QGCLabel {
                                Layout.fillWidth: true
                                text:             object.name
                            }

                            QGCButton {
                                text: qsTr("연결 해제")
                                onClicked: {
                                    QGroundControl.linkManager.disconnectLinkConfiguration(object)
                                    mainWindow.closeIndicatorDrawer()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------ Floating camera windows
    //
    // The flight map underneath stays full screen. Each camera lives in a small movable
    // window: the grip bar drags the window around, the video area keeps the
    // drag-to-gimbal gesture, and a short tap on the video toggles fullscreen.

    component CameraWindow : Item {
        id: win

        property string title
        property string detail
        property string panelKey
        property alias slot: contentSlot

        width:  root._windowWidth
        height: root._windowWidth * 9 / 16
        // Tapping a camera fills the screen with it. The column goes with it: on a tablet the
        // three windows cover a third of the width, which is most of what the operator zoomed
        // in to see. Only the map stays, small, at the bottom.
        visible: root.expandedPanel.length === 0
        z:       10

        Rectangle {
            anchors.fill: parent
            radius:       0
            color:        root._panelColor

            // Stops a tap or gimbal drag on the window from also reaching the map underneath,
            // which otherwise opened the goto-location popup on every camera switch. Same
            // guard the top bar uses. The panel's handlers sit above and still fire.
            MouseArea { anchors.fill: parent }
        }

        // The whole window is the picture; the name rides in the corner as a small chip so it
        // never eats a title bar's worth of height or splits the tap target.
        Item {
            id:              contentSlot
            anchors.fill:    parent
        }

        Rectangle {
            anchors.left:       parent.left
            anchors.top:        parent.top
            anchors.margins:    6
            width:              nameChip.implicitWidth + 14
            height:             nameChip.implicitHeight + 7
            radius:             3
            color:              "#c8121b24"
            z:                  5

            Row {
                id:                     nameChip
                anchors.centerIn:       parent
                spacing:                6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color:          "white"
                    font.bold:      true
                    font.pixelSize: Math.max(11, ScreenTools.defaultFontPixelHeight * 0.62)
                    text:           win.title
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color:          "#9fb2c4"
                    font.pixelSize: Math.max(10, ScreenTools.defaultFontPixelHeight * 0.55)
                    text:           win.detail
                    visible:        win.detail.length > 0
                }
            }
        }
    }

    CameraWindow {
        id:       primaryWindow
        panelKey: "primary"
        title:    qsTr("전방")
        // Detail text carries a value or a warning, never wiring trivia the operator
        // cannot act on.
        detail:   root._fpvConfigured ? "" : qsTr("주소 미설정")
    }

    CameraWindow {
        id:       secondaryWindow
        panelKey: "secondary"
        // Operator words, not industry ones: zoom / wide / thermal, never EO or IR.
        title:    root._aiStreamActive ? qsTr("AI 인식")
                                       : (root.eoShowsWideAngle ? qsTr("광각") : qsTr("줌"))
    }

    CameraWindow {
        id:       sharedWindow
        panelKey: "shared"
        title:    qsTr("열상")
        // At night thermal and the laser rangefinder work as a pair, so the distance lives
        // on this window's bar once readings arrive. No reading, no text.
        detail:   App.SiyiCameraController.rangefinderAvailable
                      ? qsTr("LRF %1 m").arg(Number(App.SiyiCameraController.rangefinderDistance).toFixed(1))
                      : ""
    }

    // ------------------------------------------------------------------- fly tools
    //
    // Takeoff, land, return, pause, gripper and the pre-flight checklist. QGC keeps these down
    // the left edge; the police layout dropped them with FlyViewWidgetLayer and left nothing
    // there. The guided actions resolve _guidedController off the QML context chain, which the
    // widget layer would have supplied.
    readonly property var _guidedController: guidedController

    // Stock pictograms with Korean labels. The aircraft is driven from the left strip and the
    // pod from the right one, beside the camera windows, so each side of the screen holds one
    // kind of control.
    ToolStripActionList {
        id: policeToolActions

        model: [
            PreFlightCheckListShowAction {
                text:        qsTr("점검표")
                onTriggered: root._showPreFlightChecklist()
            },
            GuidedActionTakeoff            { text: qsTr("이륙") },
            GuidedActionLand               { text: qsTr("착륙") },
            GuidedActionRTL                { text: qsTr("복귀") },
            GuidedActionPause              { text: qsTr("일시정지") },
            FlyViewAdditionalActionsButton { text: qsTr("동작") },
            FlyViewGripperButton           { text: qsTr("그리퍼") },
            ToolStripAction {
                text:               qsTr("복귀고도")
                iconSource:         "qrc:/InstrumentValueIcons/home.svg"
                // Ternary rather than &&: the controller is null before the guided layer is
                // built, and `null && x` yields undefined, which will not assign to a bool.
                enabled:            root.guidedController ? root.guidedController.showRTL : false
                dropPanelComponent: rtlAltComponent
            }
        ]
    }

    ToolStrip {
        id:                 toolStrip
        anchors.left:       parent.left
        anchors.leftMargin: 8
        anchors.top:        topBar.bottom
        anchors.topMargin:  8
        z:                  4
        // ToolStrip's own default, which still clears four Korean characters at the strip's
        // small font. The buttons size themselves to their labels inside it.
        width:              ScreenTools.defaultFontPixelWidth * 7
        maxHeight:          root.height - root._bottomInset - y - 8
        model:              policeToolActions.model
    }

    // The pod's controls: camera panel, EO sensor, zoom, shutter, AI tracking and the
    // loudspeaker. The strip's own drop panel only opens to the right, which here is the
    // camera column, so the panels are DropPanels opened by hand and told the map is the
    // viewport, which makes them drop to the left.
    ToolStripActionList {
        id: cameraToolActions

        model: [
            ToolStripAction {
                text:        qsTr("카메라")
                iconSource:  "qrc:/InstrumentValueIcons/video-camera.svg"
                visible:     QGroundControl.settingsManager.siyiCameraSettings.userVisible &&
                             QGroundControl.settingsManager.siyiCameraSettings.enabled.rawValue
                onTriggered: (source) => root._dropLeft(cameraDropPanelComponent, source)
            },
            ToolStripAction {
                // Labelled with the sensor it switches to, like the AI button.
                text:        root.eoShowsWideAngle ? qsTr("줌") : qsTr("광각")
                iconSource:  root.eoShowsWideAngle ? "qrc:/InstrumentValueIcons/zoom-in.svg"
                                                   : "qrc:/InstrumentValueIcons/zoom-out.svg"
                enabled:     App.SiyiCameraController.connected
                onTriggered: {
                    root.eoShowsWideAngle = !root.eoShowsWideAngle
                    root._applyPodStreams()
                }
            },
            ToolStripAction {
                text:        qsTr("20x")
                iconSource:  "qrc:/InstrumentValueIcons/search.svg"
                enabled:     App.SiyiCameraController.connected
                onTriggered: App.SiyiCameraController.setZoom(20)
            },
            ToolStripAction {
                text:        qsTr("중앙")
                iconSource:  "qrc:/InstrumentValueIcons/gimbal-1.svg"
                enabled:     App.SiyiCameraController.connected
                onTriggered: App.SiyiCameraController.center()
            },
            ToolStripAction {
                text:        qsTr("촬영")
                iconSource:  "qrc:/InstrumentValueIcons/camera.svg"
                enabled:     App.SiyiCameraController.connected
                onTriggered: App.SiyiCameraController.takePhoto()
            },
            ToolStripAction {
                // The AI switch: the on-device detector and the pod's recognition together.
                // Checked draws the strip's highlight, so the button reads as on or off.
                text:        qsTr("AI")
                iconSource:  "/res/police_ai.svg"
                checkable:   true
                checked:     App.PersonDetector.enabled
                onTriggered: {
                    root._setAiEnabled(!App.PersonDetector.enabled)
                    // The strip button owns its own checked state once pressed, which drops
                    // the binding above; put it back so the highlight keeps following.
                    checked = Qt.binding(() => App.PersonDetector.enabled)
                }
            },
            ToolStripAction {
                // Greyed out rather than hidden: the module drops hasTarget on a 1.5 s gap in
                // the target stream, and a button that comes and goes moves every button under
                // it while the operator is reaching for one.
                text:        qsTr("추적해제")
                iconSource:  "/res/police_ai.svg"
                enabled:     App.SiyiAiController.hasTarget
                onTriggered: App.SiyiAiController.cancelTracking()
            },
            ToolStripAction {
                text:        App.SpeakerController.playing ? qsTr("방송정지") : qsTr("경고방송")
                iconSource:  "/qmlimages/Megaphone.svg"
                visible:     QGroundControl.settingsManager.speakerSettings.enabled.rawValue
                enabled:     App.SpeakerController.connected
                onTriggered: (source) => {
                    if (App.SpeakerController.playing) {
                        App.SpeakerController.stopPlayback()
                    } else {
                        root._dropLeft(broadcastDropPanelComponent, source)
                    }
                }
            }
        ]
    }

    ToolStrip {
        id:                 cameraToolStrip
        // Against the camera column's dock, not a window: the windows can be dragged.
        x:                  root.width - root._windowWidth - 8 - width
        anchors.top:        topBar.bottom
        anchors.topMargin:  8
        // Under the left strip's click-away layer (its z - 1), so a tap here while the
        // 복귀고도 panel is open closes that panel instead of firing a camera command.
        z:                  toolStrip.z - 2
        width:              ScreenTools.defaultFontPixelWidth * 7
        // Stops above the detection card, which sits in this column's bottom corner.
        maxHeight:          aiPanel.y - 8 - y
        model:              cameraToolActions.model
    }

    // A fresh DropPanel per open, as the plan and map views do: the panel positions itself
    // once in onAboutToShow and does not reset for a second showing.
    function _dropLeft(component, button) {
        const p = button.mapToItem(root, 0, 0)
        component.createObject(root, {
            clickRect:    Qt.rect(p.x, p.y, button.width, button.height),
            dropViewPort: Qt.rect(0, topBar.height, cameraToolStrip.x, root.height - topBar.height)
        }).open()
    }

    Component {
        id: cameraDropPanelComponent

        DropPanel {
            onClosed: destroy()

            // The SIYI panel is taller than the tablet, so it scrolls inside the drop panel.
            sourceComponent: Component {
                QGCFlickable {
                    implicitWidth:  siyiPanel.implicitWidth
                    implicitHeight: Math.min(siyiPanel.implicitHeight,
                                             root.height - topBar.height - ScreenTools.defaultFontPixelHeight * 3)
                    contentHeight:  siyiPanel.implicitHeight
                    clip:           true

                    SiyiCameraControlPanel { id: siyiPanel; width: parent.width }
                }
            }
        }
    }

    Component {
        id: broadcastDropPanelComponent

        DropPanel {
            id: broadcastDropPanel

            onClosed: destroy()

            sourceComponent: Component {
                ColumnLayout {
                    spacing: 6

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        color:            "white"
                        font.bold:        true
                        font.pixelSize:   Math.max(12, ScreenTools.defaultFontPixelHeight * 0.7)
                        text:             qsTr("방송 메시지")
                    }

                    Repeater {
                        model: App.SpeakerController.messageNames

                        delegate: Button {
                            required property string modelData
                            required property int index
                            Layout.fillWidth:       true
                            Layout.preferredWidth:  Math.max(ScreenTools.minTouchPixels * 4,
                                                             ScreenTools.defaultFontPixelWidth * 14)
                            Layout.preferredHeight: root._touchHeight
                            // Track numbers are 1 based on the payload.
                            text:                   qsTr("%1. %2").arg(index + 1).arg(modelData)
                            onClicked: {
                                broadcastDropPanel.close()
                                App.SpeakerController.play(index + 1)
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth:    true
                        horizontalAlignment: Text.AlignHCenter
                        color:               "#9fb0bd"
                        font.pixelSize:      Math.max(10, ScreenTools.defaultFontPixelHeight * 0.6)
                        wrapMode:            Text.WordWrap
                        visible:             App.SpeakerController.trackCount > 0 &&
                                             App.SpeakerController.trackCount !==
                                                 App.SpeakerController.messageNames.length
                        text:                qsTr("페이로드 파일 %1개 · 이름 %2개 — 설정에서 맞춰주세요")
                                                 .arg(App.SpeakerController.trackCount)
                                                 .arg(App.SpeakerController.messageNames.length)
                    }
                }
            }
        }
    }

    function _showPreFlightChecklist() {
        if (!preFlightChecklistLoader.active) {
            preFlightChecklistLoader.active = true
        }
        preFlightChecklistLoader.item.open()
    }

    Loader {
        id:              preFlightChecklistLoader
        active:          false
        sourceComponent: preFlightChecklistPopup
    }

    Component {
        id: preFlightChecklistPopup
        FlyViewPreFlightChecklistPopup {}
    }

    // ------------------------------------------------------- MAVLink camera capture
    //
    // The gimbal's own shutter is the 촬영 button on the control bar; this is QGC's control for
    // a MAVLink camera reporting through the autopilot — photo/video mode, storage and stream
    // selection. Loaded only once a camera manager exists, which is what lets PhotoVideoControl
    // dereference it without null checks throughout.
    Loader {
        id:                  photoVideoLoader
        anchors.right:       parent.right
        anchors.rightMargin: 8
        anchors.top:         topBar.bottom
        anchors.topMargin:   8
        z:                   3
        sourceComponent:     root._activeVehicle && root._activeVehicle.cameraManager
                                 ? photoVideoComponent
                                 : undefined

        Component {
            id: photoVideoComponent
            PhotoVideoControl {}
        }
    }

    // ------------------------------------------------------------------ link loss banner
    //
    // Losing the pilot's radio is the one failure an operator must not miss, so it takes the
    // centre of the map rather than a corner badge, and it stays up until the link returns.
    Rectangle {
        id:      linkLostBanner
        visible: root._rcLinkLost || root._communicationLost
        z:       25

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top:              topBar.bottom
        anchors.topMargin:        ScreenTools.defaultFontPixelHeight * 2
        width:                    linkLostColumn.width + ScreenTools.defaultFontPixelWidth * 6
        height:                   linkLostColumn.height + ScreenTools.defaultFontPixelHeight * 1.4
        radius:                   6
        color:                    "#d31f1f"
        border.color:             "#ffffff"
        border.width:             2

        SequentialAnimation on opacity {
            running: linkLostBanner.visible
            loops:   Animation.Infinite
            NumberAnimation { to: 0.45; duration: 550 }
            NumberAnimation { to: 1.0;  duration: 550 }
        }

        Column {
            id:               linkLostColumn
            anchors.centerIn: parent
            spacing:          4

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color:                    "white"
                font.bold:                true
                font.pixelSize:           ScreenTools.defaultFontPixelHeight * 1.2
                text:                     root._rcLinkLost ? qsTr("RC 링크 끊김") : qsTr("통신 두절")
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color:                    "white"
                font.pixelSize:           Math.max(13, ScreenTools.defaultFontPixelHeight * 0.85)
                text:                     root._rcLinkLost
                                              ? qsTr("조종기 신호가 수신되지 않습니다 — 페일세이프 동작을 확인하십시오")
                                              : qsTr("기체와의 통신이 끊겼습니다")
            }
        }
    }

    // ------------------------------------------------------------- flight instruments
    //
    // Airspeed, altitude, attitude and compass are QGC's own widgets. They normally live in
    // FlyViewWidgetLayer, which this layout disables, so they are re-hosted here rather than
    // reimplemented — the telemetry bar stays user-configurable and the instrument panel keeps
    // whatever alternate QML the operator selects.
    //
    // These three names are read by the QGC widgets through the QML context chain, which the
    // police layout bypasses by not instantiating FlyViewWidgetLayer.
    readonly property var _missionController: globals.planMasterControllerFlyView
                                              ? globals.planMasterControllerFlyView.missionController
                                              : null
    readonly property bool _showSingleVehicleUI: true
    readonly property real _toolsMargin: ScreenTools.defaultFontPixelWidth * 0.75

    // Bottom left: the camera windows own the right edge, and stacking the instruments under
    // them would leave the compass hidden behind a window. Attitude and compass sit at the
    // edge with the telemetry values inboard of them. QGC's FlyViewBottomRightRowLayout puts
    // the values first and slides their background under the instrument pill, a seam that
    // only works in that order, so the row is assembled here rather than reused.
    RowLayout {
        id:                   flightInstruments
        anchors.left:         parent.left
        anchors.leftMargin:   8
        anchors.bottom:       parent.bottom
        anchors.bottomMargin: root._bottomInset
        spacing:              6
        // Below the camera windows (z 10) so a window dragged this way passes over the
        // instruments instead of disappearing behind them.
        z:                    3

        FlyViewInstrumentPanel {
            id:               instrumentPanel
            Layout.alignment: Qt.AlignBottom
            visible:          QGroundControl.corePlugin.options.flyView.showInstrumentPanel
                              && root._showSingleVehicleUI
        }

        TelemetryValuesBar {
            id:                     telemetryBar
            Layout.alignment:       Qt.AlignBottom
            settingsGroup:          factValueGrid.telemetryBarSettingsGroup
            specificVehicleForCard: null // Tracks the active vehicle
        }
    }

    // ------------------------------------------------------------- return altitude
    //
    // Opened from the strip's 복귀고도 button and drawn beside it by the strip's own drop
    // panel, which also closes it on a press anywhere else.
    Component {
        id: rtlAltComponent

        ColumnLayout {
            spacing: 6

            onVisibleChanged: customAltField.text = ""

            RowLayout {
                spacing: 5

                Repeater {
                    model: [10, 50, 100]

                    delegate: Button {
                        required property int modelData
                        Layout.preferredWidth:  Math.max(ScreenTools.minTouchPixels * 1.4,
                                                         ScreenTools.defaultFontPixelWidth * 6)
                        Layout.preferredHeight: root._touchHeight
                        text:                   qsTr("%1 m").arg(modelData)
                        onClicked: {
                            dropPanel.hide()
                            root._returnAt(modelData)
                        }
                    }
                }
            }

            RowLayout {
                spacing: 5

                TextField {
                    id:                     customAltField
                    Layout.preferredWidth:  Math.max(74, ScreenTools.defaultFontPixelWidth * 9)
                    Layout.preferredHeight: root._touchHeight
                    placeholderText:        qsTr("사용자 설정")
                    inputMethodHints:       Qt.ImhFormattedNumbersOnly
                    validator:              DoubleValidator { bottom: 1; top: 1000; decimals: 0 }
                }

                Button {
                    Layout.preferredHeight: root._touchHeight
                    text:                   qsTr("복귀")
                    enabled:                customAltField.acceptableInput
                    onClicked: {
                        const alt = Number(customAltField.text)
                        dropPanel.hide()
                        root._returnAt(alt)
                    }
                }
            }
        }
    }

    Item {
        id: fullscreenLayer
        anchors.fill: parent
        z:            20
        visible:      root.expandedPanel.length > 0

        Rectangle {
            anchors.fill: parent
            color:        "black"
            MouseArea { anchors.fill: parent }
        }

        // The map, small, in the bottom corner, so the aircraft's position stays in view while
        // a camera fills the screen. A tap swaps back: map full, camera in its window. Mirrored
        // from the live map item rather than a second map instance, the way the EO window
        // mirrors the main video; the texture is kept at the copy's own size so it costs little.
        Item {
            id:      mapPip
            width:   root._windowWidth * 0.55
            height:  width * 9 / 16
            x:       parent.width - width - 8
            y:       parent.height - height - 8
            z:       3
            visible: root.mapItem !== null

            Rectangle {
                anchors.fill: parent
                radius:       6
                color:        root._panelColor
                border.color: "#526675"
                border.width: 1
            }

            // No title bar here. It is visibly a map, it cannot be dragged (it sits in the
            // swapped window's slot), and the bar only shrank the tap target - the whole
            // point of this thing is to be tapped.
            ShaderEffectSource {
                id:              mapMirror
                anchors.fill:    parent
                anchors.margins: 2
                sourceItem:      root.mapItem
                live:            true
                textureSize:     Qt.size(Math.round(width * 2), Math.round(height * 2))
                // A centred band of the map with this window's aspect, so the copy is not
                // squeezed and the vehicle, which the map keeps near its centre, stays in it.
                sourceRect: {
                    const src = root.mapItem
                    if (!src || width <= 0 || height <= 0) {
                        return Qt.rect(0, 0, 0, 0)
                    }
                    let cw = src.width
                    let ch = cw * height / width
                    if (ch > src.height) {
                        ch = src.height
                        cw = ch * width / height
                    }
                    return Qt.rect((src.width - cw) / 2, (src.height - ch) / 2, cw, ch)
                }
            }

            TapHandler {
                // ReleaseWithinBounds takes an exclusive grab on press, so the fullscreen
                // panel's handler never also fires when the map re-docks under the release
                // point. Without this a tap on the map toggled off then straight back on.
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped:      root._toggleExpanded(root.expandedPanel)
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom:       parent.bottom
            // Above the detection card, which floats over this layer.
            anchors.bottomMargin: root._bottomInset + aiPanel.height + 12
            width:           fullscreenHint.implicitWidth + 20
            height:          fullscreenHint.implicitHeight + 12
            radius:          4
            color:           "#c0121b24"
            z:               2

            Text {
                id:             fullscreenHint
                anchors.centerIn: parent
                color:          "white"
                font.pixelSize: Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                text:           qsTr("화면 터치 또는 뒤로가기: 분할화면")
            }
        }
    }

    // Detection strip stacked on the telemetry bar, so the bottom-left corner holds all the
    // numbers and the gap beside the camera column stays free. Floats above the full screen
    // layer the way the windows do.
    PoliceDroneAiPanel {
        id: aiPanel

        // Beside the telemetry bar, same height, so the two read as one instrument row. On a
        // narrow screen the camera column takes that space, and the card sits above the bar
        // instead of running under the windows.
        readonly property real _besideX:  flightInstruments.x + telemetryBar.x + telemetryBar.width + 6
        readonly property real _rightEdge: root.width - root._windowWidth - 6
        readonly property bool _beside:   _besideX + width <= _rightEdge

        x:      Math.min(_beside ? _besideX : flightInstruments.x + telemetryBar.x,
                         _rightEdge - width)
        y:      _beside ? flightInstruments.y + telemetryBar.y
                        : flightInstruments.y + telemetryBar.y - 6 - height
        // The bar's height is configurable down to a single row, which is shorter than this
        // card's own two lines; matching it is what is wanted, being crushed by it is not.
        height: Math.max(implicitHeight, telemetryBar.height)
        z:      root.expandedPanel.length > 0 ? 21 : 3
    }

    // The forward-looking camera on the air unit's second LAN port. It is fixed to the
    // airframe, so it takes neither gimbal drag nor AI target picking.
    PoliceDroneCameraPanel {
        id:                   primaryPanel
        parent:               root.expandedPanel === "primary" ? fullscreenLayer : primaryWindow.slot
        anchors.fill:         parent
        panelTitle:           qsTr("전방")
        showChrome:           root.expandedPanel === "primary"
        streamObjectName:     "fpvVideo"
        gimbalControlEnabled: false
        onActivated:          root._toggleExpanded("primary")
    }

    PoliceDroneCameraPanel {
        id:                   secondaryPanel
        parent:               root.expandedPanel === "secondary" ? fullscreenLayer : secondaryWindow.slot
        anchors.fill:         parent
        panelTitle:           root._aiStreamActive ? qsTr("AI 인식")
                                                    : (root.eoShowsWideAngle ? qsTr("광각") : qsTr("줌"))
        showChrome:           root.expandedPanel === "secondary"
        streamObjectName:     "videoContent"
        gimbalControlEnabled: true
        personDetectionEnabled: true
        aiTargetVisible:      root.aiTargetVisible
        aiTargetX:            root.aiTargetX
        aiTargetY:            root.aiTargetY
        aiTargetWidth:        root.aiTargetWidth
        aiTargetHeight:       root.aiTargetHeight
        aiTargetLabel:        root.aiTargetLabel
        aiTargetInfo:         root.aiTargetInfo
        targetPickEnabled:    root._aiPickEnabled
        onActivated:          root._toggleExpanded("secondary")
        onTargetPicked:       (nx, ny) => App.SiyiAiController.trackPoint(nx, ny)
        onTargetBoxPicked:    (l, t, r, b) => App.SiyiAiController.trackBox(l, t, r, b)
    }

    PoliceDroneCameraPanel {
        id:                   sharedPipPanel
        parent:               root.expandedPanel === "shared" ? fullscreenLayer : sharedWindow.slot
        anchors.fill:         parent
        panelTitle:           qsTr("열상")
        showChrome:           root.expandedPanel === "shared"
        streamObjectName:     "thermalVideo"
        // No target picking here any more: this window is always thermal now, and a tap on
        // the thermal frame would hand the module coordinates from a different sensor's view.
        onActivated:          root._toggleExpanded("shared")
    }

}
