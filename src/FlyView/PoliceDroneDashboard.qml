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
    readonly property string _trackedInfo: aiTargetVisible
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

    // Where the pod says its laser is pointing, to seven decimals so it can be read off the
    // screen and compared against a surveyed point. Shown whenever the pod reports it, tracking
    // or not, because checking it is done by aiming at a known mark rather than at a person.
    // It is the LASER's point, not the tracker's — the two agree only while the tracked object
    // sits under the laser axis, and how far apart they run is exactly what has to be measured.
    readonly property string _laserInfo: App.SiyiCameraController.rangefinderTargetAvailable
        ? qsTr("레이저 지점 %1, %2")
              .arg(Number(App.SiyiCameraController.rangefinderTarget.latitude).toFixed(7))
              .arg(Number(App.SiyiCameraController.rangefinderTarget.longitude).toFixed(7))
        : ""

    readonly property string aiTargetInfo:
        (_trackedInfo.length > 0 && _laserInfo.length > 0) ? (_trackedInfo + " · " + _laserInfo)
                                                           : (_trackedInfo + _laserInfo)

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

    // ------------------------------------------------------------------ aircraft follow: the mode
    //
    // The gimbal never looks at the flight mode. None of the seven refusal reasons it can answer
    // 0xC3 with is "not GUIDED", so follow armed in LOITER answers 1 (accepted) and the aircraft
    // then sits there: a silent lie. The vendor app fills the mode in from the outside for exactly
    // this reason (UniGCS 3.1.6 viewmodels/d7.java:122-127 -> command/x.java:962-964, a plain
    // DO_SET_MODE(176) with param1=1 CUSTOM_MODE_ENABLED, param2=4 ArduCopter GUIDED; 3.0.2's
    // d6.java:118-120 is identical). It sends no setpoints of its own - the only
    // SET_POSITION_TARGET_GLOBAL_INT in that app is its "fly here" - so the SIYI air unit flies
    // the follow and the mode is the whole of our share.
    //
    // Order is 0xC3 first, mode second - the reverse of the AI module manual. ArduCopter GUIDED
    // ignores stick input, so setting the mode up front would strip the operator of manual control
    // even in the common case where the gimbal then refuses with "no target selected": slider
    // pushed, nothing moves, sticks quietly dead. Reacting to the acceptance instead means a
    // refusal leaves the mode alone.
    //
    // flightMode = gotoFlightMode rather than guidedMode = true: the latter runs
    // FirmwarePlugin::_setFlightModeAndValidate (FirmwarePlugin.cc:286-311), which spins
    // QThread::msleep(100) x 13 x 3 retries inside processEvents - up to 3.9 s of frozen screen.
    // The write here is one non-blocking sendMavCommand(MAV_CMD_DO_SET_MODE) that reports its own
    // failure (Vehicle.cc:1490-1495), and gotoFlightMode routes through the firmware plugin
    // (ArduCopterFirmwarePlugin.h:58) rather than naming a mode string here.
    //
    // Nothing switches follow off or restores the mode from this side. UniGCS drops follow on every
    // heartbeat that is not GUIDED (j3.java:20-21); copying that would let the GCS countermand a
    // pilot who took LOITER on the sticks deliberately. The panel says what disagrees; the operator
    // decides.

    // ------------------------------------------------------ aircraft follow: which stacks get it
    //
    // Only a stack whose goto mode is the one the SIYI air unit's setpoints can fly. What is
    // confirmed is ArduCopter: its gotoFlightMode() is guidedFlightMode()
    // (ArduCopterFirmwarePlugin.h:58) and the vendor app commands exactly that mode
    // (DO_SET_MODE param2=4). PX4's answers AUTO_LOITER (PX4FirmwarePlugin.cc:628), which is the
    // same mode it answers for pause (:603) - a hold, not a mode that takes position commands from
    // a third party - so pressing follow there would set a mode, move nothing, and look like it
    // worked. The test is the plugin's own two answers rather than the stack's name, and the base
    // plugin returns an empty string for both (FirmwarePlugin.h:179), so an unknown firmware falls
    // on the disabled side by itself.
    //
    // What SIYI expects on PX4 is simply not known - only the ArduCopter mode was recovered from
    // the vendor dex - so follow stays disabled there rather than guessing offboard.
    //
    // The mode pair alone was not enough. APMFirmwarePlugin::gotoFlightMode() answers "Guided" for
    // every APM stack, and ArduPlane's pauseFlightMode() is "Loiter", so a fixed wing passed this
    // test and could be put into GUIDED on a mapping confirmed only for a multirotor - while the
    // sentence explaining why follow is unavailable there could never appear. multiRotor is the
    // airframe question the paragraph above is actually asking; it is not a stack branch, and the
    // mode pair keeps doing the stack half of the job.
    readonly property bool _followModeKnown: _activeVehicle
                                             && _activeVehicle.multiRotor
                                             && (_activeVehicle.gotoFlightMode !== "")
                                             && (_activeVehicle.gotoFlightMode !== _activeVehicle.pauseFlightMode)

    // Armed and flying, the same gate every guided action in this repository puts in front of a
    // command that moves the aircraft (GuidedActionsController.qml:135 showChangeAlt, :141
    // showGotoLocation). Without it a target picked on a person standing in front of a bench-armed
    // aircraft puts that aircraft into GUIDED on the ground, and the SIYI air unit starts sending
    // position setpoints to a machine with its rotors turning.
    readonly property bool _followFlightReady: _activeVehicle && _activeVehicle.armed && _activeVehicle.flying

    // Latched when the operator slides follow on, never cleared for the rest of the session.
    //
    // What hangs off it is panel reachability: the follow panel stays reachable once follow has
    // been asked for, even after the pod link drops - the stop button in it is the only caller of
    // setAiFollow(false), and a stop that a link failure can hide is not a stop. A latch that never
    // clears is right for that and wrong for the warning below, which uses _followEngaged instead.
    //
    // Set on the slide rather than on the gimbal's acceptance: the acceptance is the message most
    // likely to be lost, and it is precisely the case where the operator needs the panel back.
    property bool _followArmed: false

    // True while the aircraft is in the GUIDED it entered for follow. Set when GUIDED and follow
    // are both true, cleared the moment the aircraft leaves GUIDED, so it never outlives the
    // condition it describes.
    //
    // _followArmed cannot do this job even though it looks like it could: it latches on the slide
    // and is never cleared for the session, so every later GUIDED the operator enters for their
    // own reasons - QGC's own goto is a GUIDED goto - wore the "sticks are dead because
    // of follow" banner for as long as the goto lasted. A banner that stands during ordinary
    // operation is a banner that gets read past.
    property bool _followEngaged: false

    // Latched the first time this session sees the aircraft outside GUIDED, and never cleared.
    // Once that has happened, every GUIDED after it started while this session was watching -
    // this GCS commanded it, or the pilot did. False means the aircraft was already in GUIDED
    // when this session found it, which is the only GUIDED nobody here can account for.
    property bool _sawNonGuided: false

    function _noteFlightMode() {
        if (!_activeVehicle) return
        _followEngaged = _activeVehicle.guidedMode && App.SiyiCameraController.aiFollowEnabled
        if (!_activeVehicle.guidedMode) _sawNonGuided = true
    }

    Connections {
        target: root._activeVehicle
        function onGuidedModeChanged() { root._noteFlightMode() }
    }

    // onGuidedModeChanged only fires on a change, and the mode a vehicle is already in when this
    // session attaches to it never changes into itself - which is precisely the state _sawNonGuided
    // is about. Read it on arrival too, here and in Component.onCompleted for a vehicle that was
    // already there when this panel was built.
    Connections {
        target: QGroundControl.multiVehicleManager
        function onActiveVehicleChanged() { root._noteFlightMode() }
    }

    // The one sentence that says the sticks are dead, kept outside the follow panel.
    //
    // It cannot live in the panel. DropPanel is a modal Popup that destroy()s itself on close
    // (followDropPanelComponent below), and a tap anywhere outside it is enough to close it - so
    // at the moment there is something to say, the Text saying it no longer exists. The panel
    // shows this same string while it is open.
    readonly property string _followModeWarning: {
        if (!root._activeVehicle) return ""
        const following = App.SiyiCameraController.aiFollowEnabled

        // GUIDED that was already running when this session arrived, with follow never once
        // observed. The gimbal keeps following across a QGC restart - the tablet can be OOM-killed
        // mid-sortie - and across a follow the hand controller started, and 0xC3 is never
        // re-queried, so aiFollowStale is still at its starting true in exactly the case where the
        // aircraft may be flying itself and nothing here can tell.
        //
        // _sawNonGuided is what keeps this off an ordinary sortie, and it is not decoration.
        // Without it the test reads "in GUIDED, and follow unobserved" - and follow is unobserved
        // for the whole of any sortie where the operator never opens the follow panel, so the
        // dashboard's own takeoff, a map Go To, orbit and ROI each wore this banner for as long as
        // they lasted. A banner that stands during ordinary operation is a banner that gets read
        // past, and the one below it means something.
        if (!root._followArmed && !root._sawNonGuided && root._activeVehicle.guidedMode
                && App.SiyiCameraController.connected && App.SiyiCameraController.aiFollowStale) {
            return qsTr("기체가 GUIDED 입니다 — 짐벌 추종 여부를 확인할 수 없습니다. 조종간이 듣지 않으면 비행모드를 바꾸십시오")
        }

        if (!root._followArmed) return ""
        if (root._activeVehicle.guidedMode) {
            // Deliberately not branched on aiFollowStale. Nothing refreshes that flag - 0xC3 is
            // answered only when asked and re-asking would switch follow back on - so it latches
            // ten seconds after the acceptance and never falls again. A banner hung off it would
            // be lit for 4:50 of a 5:00 sortie with the gimbal following perfectly, and its words
            // tell the operator to abandon follow. Staleness is reported where it costs nothing to
            // be permanent: the detection card's grey "unconfirmed".
            //
            // What is left is the case the operator can act on and cannot see anywhere else: the
            // aircraft is in follow's GUIDED and follow is not on - the usual way in is pressing
            // stop, which does not restore the flight mode (the vendor app does not either, and
            // the manual's escape is "switching flight mode can regain control").
            if (!following && root._followEngaged) {
                return qsTr("추종이 꺼졌는데 기체가 GUIDED 입니다 — 조종간이 듣지 않습니다. 비행모드를 바꾸십시오")
            }
            return ""
        }
        if (following) {
            return qsTr("짐벌은 추종 중이나 기체가 %1 입니다 — 기체는 움직이지 않습니다")
                       .arg(root._activeVehicle.flightMode)
        }
        return ""
    }

    Connections {
        target: App.SiyiCameraController
        function onAiFollowChanged() {
            // Fresh acceptance only. This signal also carries the staleness flag flipping, and
            // acting on that would re-impose GUIDED ten seconds after a pilot took the mode back.
            if (App.SiyiCameraController.aiFollowEnabled && !App.SiyiCameraController.aiFollowStale
                    && root._followModeKnown && root._followFlightReady
                    && root._activeVehicle && !root._activeVehicle.guidedMode) {
                root._activeVehicle.flightMode = root._activeVehicle.gotoFlightMode
            }
            // Follow switched on while the aircraft was already in GUIDED: guidedModeChanged will
            // not fire, so the latch has to be taken here as well.
            if (App.SiyiCameraController.aiFollowEnabled && root._activeVehicle
                    && root._activeVehicle.guidedMode) {
                root._followEngaged = true
            }
        }
    }

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
    // The menu button's touch width. Its glyph is _barIconSize like every other pictogram on
    // the bar - it was the last one still sized off the font, which on a 68 px bar drew it a
    // fifth smaller than the icons beside it. The floor is here and not on the others because
    // the others are wider than 5 mm on their contents alone: the message pictogram takes it
    // explicitly, and the GPS and link groups carry a label each.
    readonly property real _menuButtonWidth: Math.max(root._barIconSize + ScreenTools.defaultFontPixelWidth * 2,
                                                      ScreenTools.minTouchPixels)
    // Sized to its own pictograms now that QGC's toolbar indicators are not in it: the row
    // needs a touch target's height and nothing more, and every pixel saved here goes to the
    // map and the camera windows.
    readonly property real _statusHeight: PoliceBar.height
    readonly property color _panelColor:  "#e5121b24"
    /// Gap kept clear along the bottom edge now that the control panel floats rather than
    /// occupying two full-width bars.
    readonly property real _bottomInset:  8
    // The bar's proportions, pinned to the bar's own height rather than to the font. The
    // layout was drawn against a 68 px bar with 32 px pictograms and 21 px values, and a
    // ratio to the bar keeps those proportions whatever height the bar is finally given -
    // which also means enlarging the bar enlarges what the operator actually reads. The
    // floors are the desktop's, where the bar is barely half as tall.
    readonly property real  _barIconSize:     Math.max(20, root._statusHeight * 0.47)
    readonly property real  _labelSize:       Math.max(11, root._statusHeight * 0.22)
    readonly property real  _valueSize:       Math.max(12, root._statusHeight * 0.31)
    readonly property real  _statusBlockSize: Math.max(14, root._statusHeight * 0.35)
    readonly property real  _badgeSize:       Math.max(10, root._statusHeight * 0.19)
    readonly property color _labelColor:  "#9fb2c4"
    readonly property color _barColor:    PoliceBar.color
    // The whole bar is white. Colour appears in exactly two places - the status block, and a
    // value that has crossed a limit - so when something on the bar looks wrong there is one
    // block of code to read. The four bright blocks carry black text because black on a
    // saturated ground is the highest contrast available, and blue rather than a second green
    // for a flight in progress: green already means "ready to go", which flying is not.
    readonly property color _statusOkColor:   "#22c46a"
    readonly property color _statusFlyColor:  "#3aa0f5"
    readonly property color _statusIdleColor: "#33ffffff"
    readonly property color _statusInkColor:  "black"
    readonly property color _warnColor:   "#ffb02e"

    // Fixed percentages, unlike the aircraft's pack: this is the Android device's own battery,
    // there is no failsafe behind it to agree with, and the numbers do not vary by airframe.
    // 30 is roughly a shift's worth left, 10 is time to find a cable.
    readonly property int _controllerBatteryLowPercent:      30
    readonly property int _controllerBatteryCriticalPercent: 10

    // GPS_FIX_TYPE spelled out. Not Fact.enumStringValue, which returns the metadata's own
    // "3D RTK GPS Lock (fixed)" - true, and far too long for a bar. Anything past the table
    // (8 PPP) is still a fix of some kind, so it falls back to the raw number rather than to
    // a blank. A satellite count without the fix type is the number that misleads: eighteen
    // satellites and no fix is a common sight on a cold start.
    // 5 is RTK_FLOAT and 6 is RTK_FIXED, and they are not the same place: float is metre-class and
    // drifting, fixed is centimetre-class. Printing "RTK" for both means an operator flying a
    // survey on a base station watches the solution fall out of fix - multipath, a lost correction
    // stream - with nothing on the bar changing at all.
    readonly property var _gpsFixNames: ["No Fix", "No Fix", "2D", "3D", "DGPS", "RTK float", "RTK", "Static"]
    // 2 is GPS_FIX_TYPE_2D_FIX, the first value that is a position at all. One property
    // rather than the same comparison at three places, and it null-checks once: bindings in
    // the group keep evaluating after the vehicle goes away.
    readonly property bool _gpsFixed: _activeVehicle && (_activeVehicle.gps.lock.rawValue >= 2)
    readonly property string _gpsFixText: {
        if (!_activeVehicle) {
            return ""
        }
        const lock = _activeVehicle.gps.lock.rawValue
        return (lock >= 0 && lock < _gpsFixNames.length) ? _gpsFixNames[lock] : lock.toString()
    }

    // Bars, not numbers, for the pilot's radio: four steps is all an operator acts on, and the
    // step is readable at arm's length in a way that a dBm figure is not.
    readonly property bool _rcAvailable: _activeVehicle && _activeVehicle.rcRSSI.rawValue > 0 &&
                                         _activeVehicle.rcRSSI.rawValue <= 100
    readonly property int  _rcLevel:     _rcAvailable
                                             ? Math.max(1, Math.ceil(_activeVehicle.rcRSSI.rawValue / 25)) : 0

    // ------------------------------------------------------------------- the status block
    //
    // Straight out of MainStatusIndicator: healthAndArmingCheckReport first, readyToFly
    // second, sensor health and setup completion last. Three rungs exist because three
    // generations of firmware answer the question differently, not because two firmwares are
    // being told apart - every one of them is the aircraft reporting on itself, which is why
    // no branch below has to know which autopilot it is talking to.
    //
    // Armed outranks all three: what the aircraft is doing beats what it thinks it could do.
    //
    // Elapsed time is counted from _takeoffTime rather than read out of the vehicle's flightTime
    // Fact, and that is a lifetime decision, not a style one. getFact() is Q_INVOKABLE, so QML
    // takes implicit ownership of what it hands back, and FactGroup::_addFact never sets a parent
    // on the Fact (FactGroup.cc:116) - the wrapper is destructible, and collecting it deletes a
    // member that lives inside the vehicle. Holding one in a property re-evaluated every second
    // while armed is the worst version of that. _takeoffTime already carries the arming instant,
    // including the reconnect-mid-flight case, so nothing here needs the Fact at all.
    property int _flightSeconds: 0
    Timer {
        interval: 1000
        repeat:   true
        running:  root._takeoffTime !== null
        onTriggered: root._flightSeconds = Math.max(0, Math.floor((Date.now() - root._takeoffTime.getTime()) / 1000))
    }
    readonly property string _flightElapsedText: {
        const m = Math.floor(_flightSeconds / 60)
        const s = _flightSeconds % 60
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }
    readonly property var _status: {
        if (!_activeVehicle) {
            return { text: qsTr("기체 연결 안 됨"), background: _statusIdleColor, ink: "#d8dee4" }
        }
        // Above armed, because every rung below reads a value the aircraft last sent and none of
        // them expires. With the link gone the block held whatever it had been - green 시동 가능
        // on a machine nobody can talk to, or blue 비행 중 with flightTime still counting up off
        // the ground station's own clock - which is the one reading that says a dead aircraft is
        // alive. What is on screen after this is a stale value with a red block over it saying so.
        if (_communicationLost) {
            return { text: qsTr("통신 두절"), background: _alarmColor, ink: _statusInkColor }
        }
        if (_activeVehicle.armed) {
            return { text: qsTr("비행 중 %1").arg(_takeoffTime ? _flightElapsedText : ""),
                     background: _statusFlyColor, ink: _statusInkColor }
        }
        const report = _activeVehicle.healthAndArmingCheckReport
        if (report && report.supported) {
            if (!report.canArm) {
                return { text: qsTr("시동 불가"), background: _alarmColor, ink: _statusInkColor }
            }
            return report.hasWarningsOrErrors
                ? { text: qsTr("주의"),      background: _warnColor,     ink: _statusInkColor }
                : { text: qsTr("시동 가능"), background: _statusOkColor, ink: _statusInkColor }
        }
        // Amber belongs to the rung above and nowhere else. Only that one can separate "will arm,
        // with complaints" from "will not arm"; the two below answer a single yes-or-no, and this
        // block's own words are 시동 가능 / 시동 불가, so their no is 시동 불가. readyToFly is the
        // health bit of MAV_SYS_STATUS_PREARM_CHECK - the prearm checks themselves having failed,
        // which is the refusal, not a note beside it. Stock paints these amber; amber here reads
        // as "warnings, but it will still arm" and sends the operator to a switch that does
        // nothing. On an airframe without health reports - ArduPilot, which is what this is
        // delivered on - this rung is the only one that ever runs, so amber here left red
        // unreachable and the operator with no colour for a refused arm.
        if (_activeVehicle.readyToFlyAvailable) {
            return _activeVehicle.readyToFly
                ? { text: qsTr("시동 가능"), background: _statusOkColor, ink: _statusInkColor }
                : { text: qsTr("시동 불가"), background: _alarmColor,    ink: _statusInkColor }
        }
        return (_activeVehicle.allSensorsHealthy && _activeVehicle.autopilotPlugin
                && _activeVehicle.autopilotPlugin.setupComplete)
            ? { text: qsTr("시동 가능"), background: _statusOkColor, ink: _statusInkColor }
            : { text: qsTr("시동 불가"), background: _alarmColor,    ink: _statusInkColor }
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
    //
    // The worst verdict across every pack, not the verdict of the pack showing the lowest
    // percentage: those are two different questions and their answers come apart. A cell fault
    // has the autopilot call a 40 % pack CRITICAL while a healthy 35 % pack still reports OK,
    // and reading the state off the lowest-percentage pack then drew the group white with the
    // failsafe already on its way. The number displayed stays the lowest pack's - that question
    // really is "how much is left", and the lowest pack is its honest answer.
    readonly property int _batteryState: {
        if (!_activeVehicle) {
            return 0
        }
        let worst = 0
        for (let i = 0; i < _activeVehicle.batteries.count; ++i) {
            const state = _activeVehicle.batteries.get(i).chargeState.rawValue
            // 7 is CHARGING, which is not a severity: taken into the maximum it would let a pack
            // sitting on a charger outrank another pack's CRITICAL.
            if (state <= 6) {
                worst = Math.max(worst, state)
            }
        }
        return worst
    }
    // The aircraft's verdict, and only ever the aircraft's verdict. It arrives in BATTERY_STATUS
    // (BatteryFactGroupListModel.cc) measured against BATT_LOW_MV / BATT_CRT_MV, and those are
    // the numbers that actually fire the failsafe: the moment the colour changes is the moment
    // the return begins, and no threshold chosen here could put the screen and the aircraft on
    // two different plans.
    //
    // But it is often not speaking, and this bar shipped once already reading nothing else.
    // ArduPilot - the delivery airframe - leaves chargeState at MAV_BATTERY_CHARGE_STATE_UNDEFINED,
    // and a link carrying HIGH_LATENCY / HIGH_LATENCY2 fills percentRemaining and nothing else
    // (BatteryFactGroupListModel.cc:88,98). On those two paths the aircraft's verdict alone left a
    // pack at 5 % drawing plain white for the whole flight with no warning anywhere - worse than
    // any threshold this program could pick wrong. So percentage stands in, and only where the
    // aircraft has said nothing: once chargeState is set its numbers win and these are not read.
    readonly property int  kBatteryLowPercent:      30
    readonly property int  kBatteryCriticalPercent: 20
    readonly property bool _batteryStateKnown: _batteryState > 0
    readonly property bool _batteryLow: _batteryStateKnown
        ? (_batteryState === 2)
        : (!isNaN(_batteryPercent) && (_batteryPercent <= kBatteryLowPercent))
    // 3 CRITICAL to 6 UNHEALTHY.
    readonly property bool _batteryCritical: _batteryStateKnown
        ? ((_batteryState >= 3) && (_batteryState <= 6))
        : (!isNaN(_batteryPercent) && (_batteryPercent <= kBatteryCriticalPercent))

    readonly property color _batteryColor: {
        if (_batteryCritical) {
            return _alarmColor
        }
        if (_batteryLow) {
            return _warnColor
        }
        // Neither the aircraft's verdict nor a percentage low enough to stand in for one.
        // White rather than green: a wrong colour is worse than no colour, and the operator
        // would read green as the aircraft clearing the pack when the aircraft never said so.
        return "white"
    }

    /// The one line of prose on the bar: the most urgent thing wrong, or nothing at all.
    readonly property string _warningText: {
        if (!_activeVehicle) {
            return ""
        }
        // No comms-lost line and no RC-lost line: both are already on the screen, and larger -
        // the link pill on the right for the first, and for the second the flashing centre
        // banner that says the same thing in two sentences. What is left here is only ever the
        // why - the sentence that explains a red block - and repeating either of those cost the
        // takeoff row its place to say something no other part of the screen says.
        if (_batteryCritical) {
            return qsTr("배터리 위급 — 즉시 복귀하십시오")
        }
        // Why arming is refused used to be readable on the stock status indicator, which this
        // bar replaced; without it the operator is left with a button that does nothing.
        if (_activeVehicle.prearmError.length > 0) {
            return _activeVehicle.prearmError
        }
        // Below the prearm sentence, not above it: this one names no part and suggests no
        // action, so above it it would have hidden the concrete refusal behind a generic line.
        // It is still needed. The status block stops at readyToFly on the delivery airframe and
        // never reaches its sensor-health rung, so an aircraft whose ARMING_CHECK has logging
        // switched off flies with SYS_STATUS reporting the logger unhealthy while the block
        // reads green - and nothing else on the screen would mention it.
        if (!_activeVehicle.allSensorsHealthy) {
            return qsTr("센서 이상 — 기체 상태를 확인하십시오")
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

    // Measured off the bar, not off the pictogram beside it. Tied to the icon, a separator on
    // a bar half again as tall as its icons ends up a stub floating in the middle of the gap
    // instead of a rule dividing it.
    component BarSep: Rectangle {
        Layout.alignment:       Qt.AlignVCenter
        Layout.preferredWidth:  1
        Layout.preferredHeight: root._statusHeight * 0.7
        color:                  "#26ffffff"
    }

    // Every piece of text on this bar, for the sake of one line: the family. Nothing in this
    // app calls QApplication::setFont - QGCLabel puts ScreenTools.normalFontFamily on itself,
    // one label at a time - so a bare Text is drawn in the platform's default face instead,
    // which under a Korean locale is not NanumGothic and does not match the labels beside it.
    // Not only a mismatch, either: the platform default sets digits on their own widths, and
    // the status block reads "비행 중 0:12:34" once armed, so its implicitWidth moved every
    // second - and that width is what positions the message pictogram and the reason line and
    // what the logo's visibility test is measured against, so the row shuffled and the logo
    // blinked once a second at the width where that test turns over. Both faces this app ships
    // set their digits on one width.
    component BarText: Text {
        font.family: ScreenTools.normalFontFamily
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
    ///
    /// Keyed on the model, not on the connection: the pod reports connected a beat before it
    /// says what it is, and sensor routing is refused until it is known to be a ZT30, so a
    /// connectedChanged handler is one message too early and the pod keeps whatever split-screen
    /// mode it powered up in - which is how the thermal window came to show an optical sensor.
    /// A link that times out drops the cached model, so a pod that came back announces itself
    /// again and this still fires.
    Connections {
        target: App.SiyiCameraController
        function onModelChanged() {
            if (App.SiyiCameraController.isZT30) {
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
            if (App.SiyiCameraController.isZT30) {
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
        _noteFlightMode()
        // isZT30, not connected: sensor routing is refused until the pod has said what it is.
        if (App.SiyiCameraController.isZT30) {
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

    /// To the operator, "AI" is the module: it is what recognises, counts and draws boxes into the
    /// picture. The on-device detector used to ride on this switch back when it was the thing
    /// producing counts and boxes; it is down to face mosaics now, which have nothing to do with
    /// whether the module is recognising, so it has its own switch on the strip.
    function _setAiEnabled(on) {
        root._aiRequested = on
        if (App.SiyiAiController.connected) {
            App.SiyiAiController.setRecognition(on)
        }
    }

    /// What the operator last asked of the module, which is not what the module is doing: the pod
    /// boots slower than the GCS, so a switch flipped before the link is up has to be held until
    /// it comes up. Null until the switch is touched - before that the module keeps whatever it
    /// powered up with rather than being told to match a default it never heard.
    property var _aiRequested: null

    // A module that was absent when the switch was flipped never heard the command.
    Connections {
        target: App.SiyiAiController

        function onConnectedChanged() {
            // Only when the module disagrees: the pod's own panel can switch recognition too,
            // and a reconnect should not quietly undo what was set there.
            if (App.SiyiAiController.connected && (root._aiRequested !== null) &&
                    (App.SiyiAiController.recognitionEnabled !== root._aiRequested)) {
                App.SiyiAiController.setRecognition(root._aiRequested)
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
    // Three regions. The two outer ones carry the same flex weight - both fillWidth with a
    // preferred width of zero - so whatever the middle does not use is split in half. That,
    // and only that, is what puts the middle group over the centre of the bar and keeps it
    // there while a warning sentence grows on the left.
    //
    // What goes where follows who is speaking. The middle is the aircraft: its mode, its
    // satellites, its receiver, its pack. The right is the ground station's own hardware: the
    // clock, the wind, the link. The left is the verdict and the reason for it. With no
    // aircraft the middle is emptied rather than filled with dashes - a dash cannot say
    // whether a value was lost or was never there.
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

            // ------------------------------------------------------------- left region
            //
            // clip is what makes the three regions safe. Both outer regions carry the same
            // flex weight and no minimum, so each is handed exactly half of what the middle
            // leaves and nothing it contains can enlarge it. Without clip a row whose content
            // does not fit simply keeps drawing past the region's edge - over the middle
            // group, which is where the mode, the satellites and the pack are. Clipped, a
            // narrow bar loses the end of a sentence instead of overprinting the numbers.
            //
            // What gets given up first is chosen below rather than left to the cut: the brand
            // mark goes, then the takeoff row, and the reason line keeps what is left. Those
            // tests read this region's width, which is settled by the bar and the middle group
            // alone - nothing on this side feeds back into it.
            Item {
                id:                    leftRegion
                clip:                  true
                Layout.fillWidth:      true
                Layout.preferredWidth: 0
                Layout.fillHeight:     true

                RowLayout {
                    id:           leftRow
                    anchors.fill: parent
                    spacing:      ScreenTools.defaultFontPixelWidth * 1.1

                    // What this region has to hold however narrow it gets: the menu button, the
                    // status block at its own minimum, the message pictogram, and the gaps
                    // between them. Both optional items are measured against this one figure.
                    // Measured separately they each concluded on their own that there was room,
                    // and between the two thresholds both stayed on a region that could hold
                    // neither - the overflow then came out of whatever had no floor under it,
                    // which was the menu button's touch target and the takeoff time's last
                    // characters, clipped without so much as an ellipsis.
                    readonly property real _essentialWidth:
                        root._menuButtonWidth +
                        statusBlockText.implicitWidth + ScreenTools.defaultFontPixelWidth * 2 +
                        Math.max(root._barIconSize, ScreenTools.minTouchPixels) +
                        spacing * 3

                    // A plain Button paints the style's own opaque background, which read as a
                    // white slab on this dark bar. Transparent background plus an explicitly
                    // light icon matches how the toolbars in the other views render theirs.
                    Button {
                        Layout.preferredWidth:  root._menuButtonWidth
                        // _menuButtonWidth is already the floor ScreenTools puts under a touch
                        // target, so there is nothing below this to shrink into: without the
                        // minimum a RowLayout short of room takes it from here first, and a
                        // hamburger too small to hit reliably is the last thing on the bar that
                        // should pay for a long takeoff time.
                        Layout.minimumWidth:    Layout.preferredWidth
                        Layout.fillHeight:      true
                        onClicked:              root.menuRequested()

                        background: Rectangle {
                            color: parent.down ? "#33ffffff" : "transparent"
                        }

                        contentItem: Item {
                            QGCColoredImage {
                                anchors.centerIn:  parent
                                width:             root._barIconSize
                                height:            root._barIconSize
                                source:            "qrc:/qmlimages/Hamburger.svg"
                                color:             "white"
                                fillMode:          Image.PreserveAspectFit
                                sourceSize.height: height
                            }
                        }
                    }

                    // The police layout replaces FlyViewToolBar, so the brand mark lives here.
                    // First thing dropped when the region is tight: it is the only item on the
                    // bar that tells the operator nothing about the aircraft. What it has to
                    // leave room for is everything essential, whichever of the takeoff row and
                    // the reason line is up, and about ten characters beyond that.
                    Image {
                        id:                     brandLogo
                        Layout.preferredHeight: root._statusHeight * 0.36
                        Layout.preferredWidth:  Layout.preferredHeight * (1153 / 122)
                        Layout.alignment:       Qt.AlignVCenter
                        source:                 "/res/DavinciLabsLogo.png"
                        fillMode:               Image.PreserveAspectFit
                        smooth:                 true
                        // Counts the takeoff row when the takeoff row is there, which is what
                        // makes this the second thing to go and not merely the other thing to
                        // go. The dependency runs one way only - the takeoff row's own test
                        // does not look here - so there is no loop to fall into.
                        //
                        // One of the two, never both. The takeoff row is up exactly when the
                        // reason line is empty - its own test carries that term - so reserving
                        // the row and ten characters of prose at once charged this side for a
                        // pair that cannot share the bar. That surcharge is most of the logo's
                        // own width, and on the seven inch screen it put the threshold past
                        // anything the region ever reaches: the logo was hidden at every width,
                        // which is not the same thing as being the first item dropped.
                        visible:                leftRegion.width > leftRow._essentialWidth +
                                                    Layout.preferredWidth + leftRow.spacing +
                                                    (takeoffRow.visible
                                                         ? takeoffRow.implicitWidth + leftRow.spacing
                                                         : ScreenTools.defaultFontPixelWidth * 10)
                    }

                    // The first thing read and the only filled shape on the bar. Black on a
                    // bright ground rather than coloured text on the dark one: at seven inches
                    // in daylight a block of colour is found without hunting for it, and it
                    // gathers every state colour into one place on the screen and one binding
                    // in the file.
                    Rectangle {
                        Layout.alignment:       Qt.AlignVCenter
                        Layout.preferredHeight: root._statusHeight * 0.62
                        Layout.preferredWidth:  statusBlockText.implicitWidth +
                                                ScreenTools.defaultFontPixelWidth * 2
                        // The one item here that may not be squeezed. The text inside is
                        // centred and black, so a block narrower than its own word puts black
                        // letters on the bar's dark ground - unreadable, and unreadable at the
                        // exact moment the block is red.
                        Layout.minimumWidth:    Layout.preferredWidth
                        radius:                 height * 0.22
                        color:                  root._status.background

                        BarText {
                            id:               statusBlockText
                            anchors.centerIn: parent
                            color:            root._status.ink
                            font.bold:        true
                            font.pixelSize:   root._statusBlockSize
                            text:             root._status.text
                        }
                    }

                    // STATUSTEXT from the aircraft - prearm refusals, EKF and thrust warnings.
                    // Beside the status block because that is what they are for: almost every
                    // one of them is the sentence explaining why the block is not green.
                    Item {
                        Layout.alignment:       Qt.AlignVCenter
                        Layout.preferredWidth:  Math.max(root._barIconSize, ScreenTools.minTouchPixels)
                        // Same reason as the menu button: already at the touch floor, so it may
                        // not be the one that gives when the row runs short.
                        Layout.minimumWidth:    Layout.preferredWidth
                        Layout.preferredHeight: Math.min(root._statusHeight, ScreenTools.minTouchPixels)
                        // Always there once a vehicle is: opening the drawer clears the unread
                        // count, and a pictogram that vanishes on the tap that read it leaves
                        // no way back to the list. The badge, not the presence, says what is new.
                        visible:                root._activeVehicle

                        QGCColoredImage {
                            id:                messageIcon
                            anchors.centerIn:  parent
                            width:             root._barIconSize
                            height:            root._barIconSize
                            source:            "qrc:/InstrumentValueIcons/chat-bubble-dots.svg"
                            fillMode:          Image.PreserveAspectFit
                            sourceSize.height: root._barIconSize
                            color:             !root._activeVehicle                      ? root._idleColor
                                               : root._activeVehicle.messageTypeError    ? root._alarmColor
                                               : root._activeVehicle.messageTypeWarning  ? root._warnColor
                                                                                         : "white"
                        }

                        // Vehicle::messageCount, which resetAllMessages() zeroes when the
                        // drawer opens - so this counts what has not been read, not what has
                        // arrived.
                        Rectangle {
                            anchors.right:       messageIcon.right
                            anchors.top:         messageIcon.top
                            anchors.rightMargin: -root._badgeSize * 0.4
                            anchors.topMargin:   -root._badgeSize * 0.4
                            width:               Math.max(height, badgeText.implicitWidth + root._badgeSize * 0.7)
                            height:              root._badgeSize * 1.6
                            radius:              height / 2
                            color:               root._alarmColor
                            visible:             root._activeVehicle && (root._activeVehicle.messageCount > 0)

                            BarText {
                                id:               badgeText
                                anchors.centerIn: parent
                                color:            "white"
                                font.bold:        true
                                font.pixelSize:   root._badgeSize
                                text:             root._activeVehicle ? root._activeVehicle.messageCount : ""
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked:    mainWindow.showIndicatorDrawer(vehicleMessagesPage, parent)
                        }
                    }

                    // Required on the video screen for delivery: when this flight began and how
                    // many times this airframe has flown. Duration and distance are on the
                    // telemetry bar bottom left, so they are not repeated here.
                    Row {
                        id:               takeoffRow
                        Layout.alignment: Qt.AlignVCenter
                        spacing:          ScreenTools.defaultFontPixelWidth * 1.1
                        // Gives its space up the moment there is something wrong to read, and
                        // the moment its own region cannot hold it - the window width says
                        // nothing about how much of it this side was given. First of the two
                        // optional items to be granted room and last to be taken out, which is
                        // the order the logo's test above is written to follow.
                        visible:          root._activeVehicle && (root._warningText.length === 0) &&
                                          (leftRegion.width > leftRow._essentialWidth +
                                               takeoffRow.implicitWidth + leftRow.spacing)

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

                                BarText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    color:                  root._labelColor
                                    font.pixelSize:         root._labelSize
                                    text:                   logItem.modelData.label
                                }

                                BarText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    color:                  "white"
                                    font.pixelSize:         root._valueSize
                                    text:                   logItem.modelData.value
                                }
                            }
                        }
                    }

                    // The one line of prose on the bar. It answers why, never what - the block
                    // to its left has already said what. Elided inside its own region so a long
                    // prearm sentence cannot push the middle group off centre.
                    BarText {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        color:            root._alarmColor
                        font.bold:        true
                        font.pixelSize:   root._valueSize
                        elide:            Text.ElideRight
                        text:             root._warningText
                    }
                }
            }

            // ----------------------------------------------------------- centre region
            //
            // Fixed width by its own content and no fillWidth, which is what leaves the two
            // outer regions an equal share to split and so centres this one on the bar.
            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing:          ScreenTools.defaultFontPixelWidth * 1.1
                // Every value here comes from the aircraft, so with no aircraft the group goes
                // rather than each value falling back to a dash.
                visible:          root._activeVehicle

                // What the aircraft is doing. Plain white now that the state colours live in
                // the status block - a green mode name was a second answer to a question the
                // block already answers, and the two could disagree.
                Row {
                    Layout.alignment: Qt.AlignVCenter
                    spacing:          ScreenTools.defaultFontPixelWidth * 0.6

                    BarIcon {
                        source: "/qmlimages/Quad.svg"
                    }

                    BarText {
                        anchors.verticalCenter: parent.verticalCenter
                        width:                  Math.min(implicitWidth, ScreenTools.defaultFontPixelWidth * 12)
                        elide:                  Text.ElideRight
                        color:                  "white"
                        font.bold:              true
                        font.pixelSize:         root._valueSize
                        text:                   root._activeVehicle ? root._activeVehicle.flightMode : ""
                    }
                }

                BarSep {}

                // Satellites and the fix behind them. No signal bars: bars are the idiom for
                // radio strength, and a GPS fix is not a strength - eighteen satellites with no
                // fix is an ordinary sight on a cold start and would have drawn four full bars.
                // A tap opens QGC's own GPS page, which this bar replaces.
                Item {
                    Layout.alignment:       Qt.AlignVCenter
                    Layout.preferredWidth:  gpsRow.implicitWidth
                    Layout.preferredHeight: Math.min(root._statusHeight, ScreenTools.minTouchPixels)

                    Row {
                        id:               gpsRow
                        anchors.centerIn: parent
                        spacing:          ScreenTools.defaultFontPixelWidth * 0.5

                        BarIcon {
                            source: "/qmlimages/Gps.svg"
                            tint:   root._gpsFixed ? "white" : root._warnColor
                        }

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  root._gpsFixed ? "white" : root._warnColor
                            font.bold:              true
                            font.pixelSize:         root._valueSize
                            text:                   root._activeVehicle
                                                        ? root._activeVehicle.gps.count.valueString : ""
                        }

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  root._gpsFixed ? "white" : root._warnColor
                            font.pixelSize:         root._labelSize
                            text:                   root._gpsFixText
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    mainWindow.showIndicatorDrawer(gpsDetailPage, parent)
                    }
                }

                BarSep { visible: root._rcAvailable }

                // The pilot's radio. Bars here, because this one really is a signal strength.
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

                BarSep { visible: root._lowestBattery }

                // Battery: the only number on the bar that changes an operator's plan, so it
                // keeps its digits. Upright cell, deliberately a different shape from the
                // controller's flat one on the right - two identical glyphs are how 58% and 38%
                // get read the wrong way round.
                Row {
                    Layout.alignment: Qt.AlignVCenter
                    spacing:          ScreenTools.defaultFontPixelWidth * 0.5
                    visible:          root._lowestBattery

                    BarIcon {
                        source: "/qmlimages/Battery.svg"
                        tint:   root._batteryColor
                    }

                    BarText {
                        anchors.verticalCenter: parent.verticalCenter
                        color:                  root._batteryColor
                        font.bold:              true
                        font.pixelSize:         root._valueSize
                        // Percentage when the pack reports one, its voltage when it does not.
                        // Bindings run whether or not the group is visible, so the pack is
                        // checked here too rather than only in the group's visibility.
                        text:                   !root._lowestBattery
                                                    ? ""
                                                    : (isNaN(root._batteryPercent)
                                                           ? root._lowestBattery.voltage.valueString + qsTr(" V")
                                                           : qsTr("%1 %").arg(Math.round(root._batteryPercent)))
                    }
                }
            }

            // ------------------------------------------------------------ right region
            //
            // The ground station's own side. Right-anchored inside a region that carries the
            // same flex weight as the left one, so the group hugs the edge without taking part
            // in the arithmetic that centres the middle. Clipped for the same reason the left
            // side is: anchored to the right edge, a row too wide for its region grows towards
            // the middle group, and the middle group is where the aircraft's numbers are. The
            // cut then falls on the left end of this row, which is where the least urgent
            // thing on it sits.
            Item {
                id:                    rightRegion
                clip:                  true
                Layout.fillWidth:      true
                Layout.preferredWidth: 0
                Layout.fillHeight:     true

                RowLayout {
                    id:                     rightRow
                    anchors.right:          parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing:                ScreenTools.defaultFontPixelWidth * 1.1

                    // Everything on this side except the clock, counted from what is on the bar
                    // rather than from the full set. A fixed six gaps was the count for all seven
                    // items showing, which is a state the desktop never reaches - no controller
                    // pack, and no wind estimate from an airframe that does not send one leaves
                    // three items and two gaps - so four spacings that were never drawn were
                    // subtracted anyway and the clock vanished with its own room still free. The
                    // separators are in the sum now too; they were not before. The clock itself
                    // is skipped so that hiding it cannot change the number that hides it, and
                    // preferred/implicit widths are used rather than laid-out ones for the same
                    // reason - a laid-out width is an output of the visibility this feeds.
                    readonly property real _widthWithoutClock: {
                        let total = 0
                        let skipped = 0
                        for (let i = 0; i < children.length; ++i) {
                            const item = children[i]
                            // Skipped by identity, and before any property of them is read. The
                            // clock because this figure is what decides whether it is shown; the
                            // three separators because they all follow the clock now, so reading
                            // their visibility here would make this sum depend on its own result.
                            if ((item === clockSep) || (item === batterySep) || (item === linkSep)) {
                                skipped += 1
                                continue
                            }
                            if ((item === barClock) || !item.visible) {
                                continue
                            }
                            const preferred = item.Layout.preferredWidth
                            total += ((preferred > 0) ? preferred : item.implicitWidth) + spacing
                        }
                        // Counted flat instead of dropped. Dropping them lost a pixel and a gap
                        // apiece - about thirty across the three - and this row has no width of
                        // its own: it grows to implicitWidth and the region clips it from the
                        // LEFT, where the clock is. Undercounting therefore shows a clock that is
                        // then sliced in half, which is the exact thing this figure exists to
                        // prevent. Over-counting a hidden separator only hides the clock early,
                        // and a missing clock reads as a missing clock.
                        return total + (skipped * (1 + spacing))
                    }

                    // Wall clock. Every entry in the aircraft's message list and every log line
                    // is stamped, and a stamp is only worth carrying if the same clock is
                    // readable on the bar.
                    BarText {
                        id:               barClock
                        Layout.alignment: Qt.AlignVCenter
                        // The one thing on this side the operator can also read off the
                        // tablet's own status bar, so it is what goes when the region cannot
                        // hold the row - half a clock cut by the region edge is worse than no
                        // clock. Measured against what is actually beside it, which never
                        // depends on this one, so the test cannot feed itself.
                        visible:          rightRegion.width - implicitWidth -
                                          rightRow._widthWithoutClock > 0
                        color:            "white"
                        font.bold:        true
                        font.pixelSize:   root._valueSize
                        text:             Qt.formatTime(new Date(), "HH:mm:ss")

                        Timer {
                            interval:    1000
                            running:     true
                            repeat:      true
                            onTriggered: barClock.text = Qt.formatTime(new Date(), "HH:mm:ss")
                        }
                    }

                    // Only where there is something on its left to divide from. The clock is
                    // the one item on this side that hides itself, and when it did this rule
                    // became the first thing in the row - a stroke drawn down the edge of a
                    // group it was not separating from anything.
                    BarSep {
                        id:      clockSep
                        visible: windGroup.visible && barClock.visible
                    }

                    // The aircraft's own wind estimate, which costs no network and describes the
                    // air the aircraft is actually in rather than the air over the airfield.
                    // telemetryAvailable stays false until an estimate arrives, so an airframe
                    // that does not estimate wind shows nothing here instead of a reading of
                    // zero it never made.
                    //
                    // A bearing in figures, not a turning arrow. Nothing on this bar points
                    // north and the map below can be turned heading-up, so a rotated arrow has
                    // no reference to be read against - and the two ends of it mean opposite
                    // things: 180 with the arrow down could be wind out of the south or wind
                    // towards it, which is the difference between planning the return into the
                    // wind and planning it downwind. "180°에서" says which. The convention is
                    // the MAVLink WIND message's, the one ArduPilot sends: the bearing the
                    // wind is coming from. (PX4's WIND_COV carries the opposite sense into the
                    // same Fact - see VehicleWindFactGroup - so this label is right for the
                    // airframe this bar is built for and would need the +180 for that one.)
                    Row {
                        id:               windGroup
                        Layout.alignment: Qt.AlignVCenter
                        spacing:          ScreenTools.defaultFontPixelWidth * 0.5
                        visible:          root._activeVehicle && root._activeVehicle.wind.telemetryAvailable

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  "white"
                            font.bold:              true
                            font.pixelSize:         root._valueSize
                            // HIGH_LATENCY carries a wind speed and no bearing at all, and the
                            // Fact starts as NaN, so the bearing is printed only once there is
                            // one to print rather than as "NaN°".
                            text: {
                                if (!root._activeVehicle) {
                                    return ""
                                }
                                const wind = root._activeVehicle.wind
                                const speed = wind.speed.valueString + " " + wind.speed.units
                                return isNaN(wind.direction.rawValue)
                                           ? speed
                                           : qsTr("%1°에서 %2").arg(Math.round(wind.direction.rawValue)).arg(speed)
                            }
                        }
                    }

                    // Same rule as the clock's separator, for the same reason: it has to have
                    // something on its left, not merely something on its right. With no wind
                    // estimate and the clock squeezed out - a laptop desk, or a narrow bar -
                    // this was the first item in the row, a rule drawn down the region's edge.
                    BarSep {
                        id:      batterySep
                        visible: controllerBatteryGroup.visible &&
                                 (barClock.visible || windGroup.visible)
                    }

                    // The controller's own battery. Flat cell on its side, deliberately not the
                    // aircraft's upright one: two identical glyphs a few centimetres apart is
                    // how 58% and 38% get read the wrong way round. Absent everywhere there is
                    // no such battery to read - a laptop control desk is a real deployment -
                    // and absent rather than zero, because a controller reading 0% is a
                    // controller about to go dark.
                    Row {
                        id:               controllerBatteryGroup
                        Layout.alignment: Qt.AlignVCenter
                        spacing:          ScreenTools.defaultFontPixelWidth * 0.5
                        visible:          App.ControllerBattery.percent >= 0

                        readonly property color tint:
                            App.ControllerBattery.percent <= root._controllerBatteryCriticalPercent ? root._alarmColor
                            : App.ControllerBattery.percent <= root._controllerBatteryLowPercent    ? root._warnColor
                                                                                                   : "white"

                        BarIcon {
                            source: "qrc:/InstrumentValueIcons/battery-full.svg"
                            tint:   controllerBatteryGroup.tint
                        }

                        BarText {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  controllerBatteryGroup.tint
                            font.bold:              true
                            font.pixelSize:         root._valueSize
                            text:                   qsTr("%1 %").arg(App.ControllerBattery.percent)
                        }
                    }

                    // Unconditional, this one divided the link group from nothing at all: it is
                    // the last separator in the row, so with the clock hidden, no wind estimate
                    // and no controller pack it stood alone at the head of the row.
                    BarSep {
                        id:      linkSep
                        visible: barClock.visible || windGroup.visible ||
                                 controllerBatteryGroup.visible
                    }

                    // The link, and the only thing on the bar that is also a button: with no
                    // vehicle it opens QGC's link chooser, with one it lists the links that are
                    // up, each with its own disconnect. A chain, because that is what a link is.
                    // Not a wifi fan - the fan says how strong a radio is, and this says only
                    // whether packets are arriving.
                    Item {
                        id:                     linkIndicator
                        Layout.alignment:       Qt.AlignVCenter
                        Layout.preferredWidth:  linkRow.implicitWidth + ScreenTools.defaultFontPixelWidth
                        Layout.preferredHeight: Math.min(root._statusHeight, ScreenTools.minTouchPixels)

                        // White while nothing is wrong, including before anything is attached:
                        // not yet connected is not a fault, and red is reserved for a link that
                        // was there and went.
                        readonly property color tint: (root._activeVehicle && !root._linkUp)
                                                          ? root._alarmColor : "white"

                        Row {
                            id:               linkRow
                            anchors.centerIn: parent
                            spacing:          ScreenTools.defaultFontPixelWidth * 0.6

                            Item {
                                anchors.verticalCenter: parent.verticalCenter
                                width:                  root._barIconSize
                                height:                 root._barIconSize

                                BarIcon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    source:                   "qrc:/InstrumentValueIcons/link.svg"
                                    tint:                     linkIndicator.tint
                                }

                                // Drawn rather than fetched: one diagonal rule is not worth a
                                // second copy of the chain in the resources.
                                Rectangle {
                                    anchors.centerIn: parent
                                    width:            parent.width * 1.2
                                    height:           Math.max(2, root._barIconSize * 0.09)
                                    radius:           height / 2
                                    rotation:         45
                                    color:            linkIndicator.tint
                                    visible:          root._activeVehicle && !root._linkUp
                                }
                            }

                            BarText {
                                anchors.verticalCenter: parent.verticalCenter
                                color:                  linkIndicator.tint
                                font.bold:              true
                                font.pixelSize:         root._valueSize
                                text:                   !root._activeVehicle
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
                // AI pins the main stream to the zoom camera, so with the module on this
                // button would snap straight back to 광각 with nothing said to the operator.
                enabled:     App.SiyiCameraController.connected &&
                             !QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue
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
                // Label follows the pod's own recording state, so a start that never took does not
                // sit here reading as recording.
                text:        App.SiyiCameraController.recording ? qsTr("녹화중지") : qsTr("녹화")
                iconSource:  "qrc:/InstrumentValueIcons/film.svg"
                enabled:     App.SiyiCameraController.connected
                onTriggered: App.SiyiCameraController.toggleRecording()
            },
            ToolStripAction {
                // The pod's AI module, and only it. Checked follows the module's own answer rather
                // than the press, so a module that is absent or that refused the stream resolution
                // does not sit here reading as armed.
                text:        qsTr("AI")
                iconSource:  "/res/police_ai.svg"
                checkable:   true
                checked:     App.SiyiAiController.recognitionEnabled
                onTriggered: {
                    root._setAiEnabled(!App.SiyiAiController.recognitionEnabled)
                    // The strip button owns its own checked state once pressed, which drops
                    // the binding above; put it back so the highlight keeps following.
                    checked = Qt.binding(() => App.SiyiAiController.recognitionEnabled)
                }
            },
            ToolStripAction {
                // The face mosaic, which is all the on-device detector does now. Its own switch
                // rather than a rider on AI above: it is a personal-data measure that outlives any
                // recognition setting, and it is the one control here that costs about 100 ms of
                // tablet CPU per frame, so the operator needs to be able to drop it on its own.
                //
                // Labelled with what pressing it does, like 경고방송, rather than made checkable:
                // ToolStrip clears every other checked button when one is checked, so a second
                // checkable action in this strip would knock the AI highlight out and take its
                // binding with it.
                //
                // Three labels, not two. The switch being on does not mean anything is being
                // mosaicked: a model that failed to load leaves active false with enabled true, the
                // overlay draws nothing, and a label reading only the switch would say the faces
                // are covered while they are on screen. active is the detector's own answer.
                text:        !App.PersonDetector.enabled ? qsTr("모자이크")
                             : App.PersonDetector.active ? qsTr("모자이크끔")
                                                         : qsTr("모자이크 불가")
                iconSource:  App.PersonDetector.active ? "qrc:/InstrumentValueIcons/view-show.svg"
                                                       : "qrc:/InstrumentValueIcons/view-hide.svg"
                onTriggered: App.PersonDetector.enabled = !App.PersonDetector.enabled
            },
            ToolStripAction {
                // Aircraft follow. This one flies the aircraft, so it opens a panel to slide for
                // confirmation instead of acting on the press; the panel carries the stop button
                // and the gimbal's refusal reason, and the follow state itself is on the detection
                // card, where the operator is already reading the pod's state.
                text:        qsTr("추종")
                iconSource:  "qrc:/InstrumentValueIcons/drone.svg"
                // Link state gates starting follow, never reaching it. Once follow has been asked
                // for, this button is the only route to the stop button and to the GUIDED warning,
                // and the pod link dropping for two seconds is exactly when both are needed - a
                // stop that a blipped link can lock away is not a stop.
                enabled:     App.SiyiCameraController.connected || root._followArmed
                onTriggered: (source) => root._dropLeft(followDropPanelComponent, source)
            },
            ToolStripAction {
                // Greyed out rather than hidden: the module drops hasTarget on a 1.5 s gap in
                // the target stream, and a button that comes and goes moves every button under
                // it while the operator is reaching for one.
                text:        qsTr("추적해제")
                // A reticle, not the AI glyph the switch above already wears: two buttons with
                // the same picture read as two halves of one control.
                iconSource:  "/qmlimages/TrackingIcon.svg"
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
        // Stops above the detection card, but only where the card is actually underneath: on a
        // screen wide enough for the card to sit clear of this column, deferring to it anyway
        // clipped the top of the strip off.
        maxHeight: {
            const clearsCard = (aiPanel.x >= x + width) || (aiPanel.x + aiPanel.width <= x)
            const floor = clearsCard ? (root.height - root._bottomInset) : (aiPanel.y - 8)
            return floor - y
        }
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

    Component {
        id: followDropPanelComponent

        DropPanel {
            id: followDropPanel

            onClosed: destroy()

            // Deliberately not closed on accept, unlike the broadcast panel: the gimbal checks the
            // preconditions itself and answers with a numbered refusal, and that answer lands after
            // the gesture is over. A panel that closed on the slide would take the only place the
            // reason is shown away with it.
            sourceComponent: Component {
                ColumnLayout {
                    id:      followColumn
                    spacing: 6

                    readonly property real _panelWidth: ScreenTools.defaultFontPixelWidth * 24

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        color:            "white"
                        font.bold:        true
                        font.pixelSize:   Math.max(12, ScreenTools.defaultFontPixelHeight * 0.7)
                        text:             qsTr("기체 추종")
                    }

                    Text {
                        Layout.preferredWidth: followColumn._panelWidth
                        horizontalAlignment:   Text.AlignHCenter
                        wrapMode:              Text.WordWrap
                        color:                 "#9fb0bd"
                        font.pixelSize:        Math.max(10, ScreenTools.defaultFontPixelHeight * 0.6)
                        text:                  qsTr("짐벌이 추적 중인 표적을 기체가 따라갑니다. 기체가 실제로 움직입니다. 짐벌이 수락하면 비행모드가 GUIDED 로 바뀌며, GUIDED 동안 조종간 입력은 듣지 않습니다. 조종을 되찾으려면 비행모드를 직접 바꾸십시오.")
                    }

                    // Same slide gesture as takeoff and as the target confirmation on the video
                    // window, for the same reason: this one moves the aircraft. Kept a gesture even
                    // though it now also changes the flight mode - that raises the stakes, it does
                    // not lower them.
                    SliderSwitch {
                        Layout.alignment:      Qt.AlignHCenter
                        Layout.preferredWidth: followColumn._panelWidth
                        // Unconfirmed counts as off here. aiFollowEnabled alone hid the slider for
                        // the rest of the session whenever the gimbal stopped answering, since the
                        // only things that clear it are a 0xC3 reply and a stop - and re-sending
                        // 0xC3{1} to a gimbal that is already following costs nothing.
                        visible:               !App.SiyiCameraController.aiFollowEnabled
                                               || App.SiyiCameraController.aiFollowStale
                        // An airframe with no guided support would take the 0xC3 and then never get
                        // the mode that makes it move, which is the silent failure this panel exists
                        // to avoid. Armed and flying on top of it, the same gate the rest of the
                        // repository puts in front of a guided action; the aircraft is not the
                        // gimbal's to fly while it is on the ground. See root._followFlightReady
                        // and root._followModeKnown.
                        enabled:               root._activeVehicle
                                               && root._activeVehicle.supports.guidedMode
                                               && root._followModeKnown
                                               && root._followFlightReady
                        opacity:               enabled ? 1 : 0.4
                        confirmText:           qsTr("밀어서 추종 시작 (GUIDED 전환)")
                        onAccept: {
                            // Latched before the send: what makes the panel reachable again is the
                            // asking, not the answering. See root._followArmed.
                            root._followArmed = true
                            App.SiyiCameraController.setAiFollow(true)
                        }
                    }

                    // Stopping needs no confirmation: it only ever puts the aircraft back where it
                    // was before the slide, and a stop gesture that can be fumbled is worse.
                    //
                    // Always visible, never gated on aiFollowEnabled. That flag is one UDP datagram
                    // deep - a dropped 0xC3 reply leaves it false while the gimbal is still flying
                    // the aircraft, and this button is the only caller of setAiFollow(false) in the
                    // repository, with no polling to recover the state. Gating it would strand the
                    // operator with a following aircraft and no stop for the rest of the session.
                    // A redundant stop costs nothing; a missing one costs the airframe.
                    Button {
                        Layout.alignment:       Qt.AlignHCenter
                        Layout.preferredWidth:  followColumn._panelWidth
                        Layout.preferredHeight: root._touchHeight
                        text:                   qsTr("추종 중지")
                        // The panel stays open, for the same reason the slide leaves it open. The
                        // stop is one datagram on a link with no retransmission; closing on the
                        // press destroy()s the panel (onClosed: destroy()) and with it the only
                        // place that says the stop has not been confirmed and the only button that
                        // can send it again. The controller repeats the datagram a few times by
                        // itself; the operator has to be able to see that and to press again.
                        onClicked: App.SiyiCameraController.setAiFollow(false)
                    }

                    Text {
                        Layout.preferredWidth: followColumn._panelWidth
                        horizontalAlignment:   Text.AlignHCenter
                        wrapMode:              Text.WordWrap
                        font.pixelSize:        Math.max(10, ScreenTools.defaultFontPixelHeight * 0.6)
                        // Three outcomes, not one "waiting". The repeats are spent inside 1.5 s,
                        // and dropping the line at that point left a stop whose every datagram
                        // died on a blipped link looking exactly like a confirmed one - the start
                        // text below even reappeared under it - while the gimbal went on flying the
                        // aircraft. A stop that could not be sent at all is a third sentence again:
                        // there is nothing in flight to wait for.
                        visible:               App.SiyiCameraController.aiFollowStopState !==
                                               App.SiyiCameraController.StopIdle
                        color:                 App.SiyiCameraController.aiFollowStopState ===
                                               App.SiyiCameraController.StopPending ? "#9fb0bd" : "#ff9c46"
                        text: {
                            switch (App.SiyiCameraController.aiFollowStopState) {
                            case App.SiyiCameraController.StopPending:
                                return qsTr("중지 확인 대기 — 짐벌 응답을 기다리는 중입니다")
                            case App.SiyiCameraController.StopUnsent:
                                return qsTr("중지 명령을 보내지 못했습니다 — 카메라 링크가 없습니다. 비행모드를 바꾸어 조종을 회복하십시오")
                            default:
                                return qsTr("중지 응답을 받지 못했습니다 — 다시 누르거나 비행모드를 바꾸십시오")
                            }
                        }
                    }

                    // The gimbal does its own precondition checks - GPS fix, a selected target,
                    // mounting orientation, model support - and refuses with a reason code. Copying
                    // those checks up here would only be a second, staler opinion; showing the
                    // reason it gave is the whole job.
                    Text {
                        Layout.preferredWidth: followColumn._panelWidth
                        horizontalAlignment:   Text.AlignHCenter
                        wrapMode:              Text.WordWrap
                        color:                 "#ff9c46"
                        font.pixelSize:        Math.max(10, ScreenTools.defaultFontPixelHeight * 0.62)
                        visible:               text.length > 0
                        text: {
                            const code = App.SiyiCameraController.aiFollowError
                            switch (code) {
                            case App.SiyiCameraController.TargetTooFarOrLow:
                                return qsTr("표적이 너무 멀거나 낮습니다")
                            case App.SiyiCameraController.AiTrackingDisabled:
                                return qsTr("짐벌의 AI 추적이 꺼져 있습니다")
                            case App.SiyiCameraController.GpsDataMissing:
                                return qsTr("GPS 데이터가 없습니다")
                            case App.SiyiCameraController.TargetTooCloseOrHigh:
                                return qsTr("표적이 너무 가깝거나 높습니다")
                            case App.SiyiCameraController.InvertedModeUnsupported:
                                return qsTr("역방향 장착에서는 추종을 지원하지 않습니다")
                            case App.SiyiCameraController.TargetNotSelected:
                                return qsTr("추적할 표적이 선택되지 않았습니다")
                            case App.SiyiCameraController.ModelUnsupported:
                                return qsTr("이 짐벌 모델은 추종을 지원하지 않습니다")
                            // Anything else with a code, including a reason a later firmware adds,
                            // is still named: silence here leaves a refusal the operator cannot act on.
                            default:
                                return (code === 0) ? ""
                                                    : qsTr("짐벌이 알 수 없는 사유로 거절했습니다 (코드 %1)").arg(code)
                            }
                        }
                    }

                    // The gimbal's follow and the aircraft's flight mode are set by two separate
                    // commands on two separate links, so they can disagree in both directions, and
                    // both readings are ones the operator cannot get anywhere else on this screen.
                    //
                    // The wording is root._followModeWarning, shared with the banner on the map:
                    // this panel is destroyed on close, so the panel copy is the convenience and
                    // the banner is the one that has to be there. Follow stopping does not restore
                    // the flight mode - the vendor app does not restore it either (d7.java:122
                    // guards its mode set with the enabling branch alone), and the AI module manual
                    // is explicit that "switching flight mode can regain control", i.e. escape is
                    // the operator's job. No automatic restore here: only the pilot knows which
                    // mode they want.
                    Text {
                        Layout.preferredWidth: followColumn._panelWidth
                        horizontalAlignment:   Text.AlignHCenter
                        wrapMode:              Text.WordWrap
                        color:                 "#ffb020"
                        font.bold:             true
                        font.pixelSize:        Math.max(10, ScreenTools.defaultFontPixelHeight * 0.62)
                        visible:               text.length > 0
                        text:                  root._followModeWarning
                    }

                    // Why the slide is greyed out, when it is. A disabled control with no reason
                    // beside it is the same silent nothing this panel exists to prevent.
                    Text {
                        Layout.preferredWidth: followColumn._panelWidth
                        horizontalAlignment:   Text.AlignHCenter
                        wrapMode:              Text.WordWrap
                        color:                 "#9fb0bd"
                        font.pixelSize:        Math.max(10, ScreenTools.defaultFontPixelHeight * 0.6)
                        visible:               text.length > 0
                        text: {
                            if (App.SiyiCameraController.aiFollowEnabled) return ""
                            if (!root._activeVehicle) return qsTr("기체가 연결되지 않았습니다")
                            if (!root._activeVehicle.supports.guidedMode) {
                                return qsTr("이 기체는 GUIDED 를 지원하지 않습니다")
                            }
                            if (!root._followModeKnown) {
                                return qsTr("이 비행 스택에서 추종이 쓸 비행모드를 확인할 수 없어 사용할 수 없습니다 (ArduCopter 만 확인됨)")
                            }
                            if (!root._followFlightReady) {
                                return qsTr("비행 중에만 사용할 수 있습니다")
                            }
                            return ""
                        }
                    }

                    // Nothing confirmed the send. Old gimbal firmware does not answer this command
                    // at all, so an unchecked button after a slide means "no reply", not "refused".
                    // Once confirmed, the same slot carries the staleness case: the confirmation
                    // aged out, and since the gimbal drops follow on its own without saying so, an
                    // aged confirmation is no longer evidence the aircraft is being flown.
                    Text {
                        Layout.preferredWidth: followColumn._panelWidth
                        horizontalAlignment:   Text.AlignHCenter
                        wrapMode:              Text.WordWrap
                        color:                 "#9fb0bd"
                        font.pixelSize:        Math.max(10, ScreenTools.defaultFontPixelHeight * 0.6)
                        // Silent while a stop is unresolved: the line above owns that case, and
                        // this one talks about starting.
                        visible:               (!App.SiyiCameraController.aiFollowEnabled ||
                                                App.SiyiCameraController.aiFollowStale) &&
                                               App.SiyiCameraController.aiFollowError === 0 &&
                                               App.SiyiCameraController.aiFollowStopState ===
                                               App.SiyiCameraController.StopIdle
                        text:                  App.SiyiCameraController.aiFollowStale
                                               ? qsTr("짐벌이 최근 응답하지 않아 추종 여부를 확인할 수 없습니다. 확실히 멈추려면 추종 중지를 누르십시오.")
                                               : qsTr("짐벌이 확인해야 켜집니다. 몇 초 안에 켜지지 않으면 짐벌이 응답하지 않는 것입니다.")
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

    // ------------------------------------------------------------ follow / GUIDED mismatch
    //
    // GUIDED ignores the sticks and nothing on this side ever puts the aircraft back - the manual's
    // own escape is "switching flight mode can regain control", i.e. the operator's job. So the
    // sentence that says so has to be on screen whether or not the follow panel is open, and the
    // panel is a Popup that destroys itself the moment the stop button closes it. Same treatment as
    // the link banner, one step down in weight: amber rather than red, and no blink, because the
    // aircraft is still flying and the operator has a mode switch to reach for.
    Rectangle {
        id:      followModeBanner
        // No guidedMode term. _followModeWarning already encodes "nothing to say" as an empty
        // string and it null-checks the vehicle itself, so the extra term was not a filter but a
        // contradiction: the third branch of that string is built only when guidedMode is false,
        // so the one sentence that reports "the gimbal is chasing a target and the aircraft is
        // sitting in Loiter" could never reach the banner, and the only other place it appears is
        // the follow panel, a Popup that destroy()s itself on close.
        visible: root._followModeWarning.length > 0
        z:       25

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top:              linkLostBanner.visible ? linkLostBanner.bottom : topBar.bottom
        anchors.topMargin:        ScreenTools.defaultFontPixelHeight
        width:                    followModeText.implicitWidth + ScreenTools.defaultFontPixelWidth * 4
        height:                   followModeText.implicitHeight + ScreenTools.defaultFontPixelHeight
        radius:                   6
        color:                    "#b35c00"
        border.color:             "#ffffff"
        border.width:             2

        Text {
            id:                  followModeText
            anchors.centerIn:    parent
            width:               Math.min(implicitWidth, root.width * 0.6)
            horizontalAlignment: Text.AlignHCenter
            wrapMode:            Text.WordWrap
            color:               "white"
            font.bold:           true
            font.pixelSize:      Math.max(13, ScreenTools.defaultFontPixelHeight * 0.85)
            text:                root._followModeWarning
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
        // Named for the sensor on screen, not the pipe it came through: AI pins the main
        // stream to the zoom camera, so its feed is the zoom picture with boxes drawn in.
        panelTitle:           root._aiStreamActive ? qsTr("줌 · AI")
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
        // The module reads selections in the stream's own resolution and never reports what
        // that is; left at its 1280x720 default a tap on a 1080p stream lands a third in.
        // Assigned through the properties: the setters are not callable from QML.
        onStreamRectChanged: {
            const frame = secondaryPanel.streamRect
            if ((frame.width > 0) && (frame.height > 0)) {
                App.SiyiAiController.streamWidth  = frame.width
                App.SiyiAiController.streamHeight = frame.height
            }
        }
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
