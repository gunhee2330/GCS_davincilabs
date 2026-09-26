import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.FlightMap
import QGroundControl.FlyView
import QGroundControl.GeoMap
import QGroundControl.SiyiCamera
import QGroundControl.Toolbar
import QGroundControl.Viewer3D

Item {
    id:         root
    objectName: "policeDroneDashboard"

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
    readonly property real _menuButtonWidth: PoliceBar.menuButtonWidth
    // Sized to its own pictograms now that QGC's toolbar indicators are not in it: the row
    // needs a touch target's height and nothing more, and every pixel saved here goes to the
    // map and the camera windows.
    readonly property real _statusHeight: PoliceBar.height
    readonly property color _panelColor:  "#e5121b24"
    /// Gap kept clear along the bottom edge now that the control panel floats rather than
    /// occupying two full-width bars.
    readonly property real _bottomInset:  8

    /// Whether the camera strip is unfolded. Folded, only its handle is left on the screen.
    property bool cameraStripOpen: true
    // The bar's proportions, pinned to the bar's own height rather than to the font. The
    // layout was drawn against a 68 px bar with 32 px pictograms and 21 px values, and a
    // ratio to the bar keeps those proportions whatever height the bar is finally given -
    // which also means enlarging the bar enlarges what the operator actually reads. The
    // floors are the desktop's, where the bar is barely half as tall.
    // Just under the cap height of the value beside it, so the pictogram reads as a label on
    // that number rather than as the thing being looked at. Sized off that value and not off
    // the bar: the mockup sets a 30 px pictogram against a 32 px number, and PoliceBar's own
    // 0.13 of the bar draws barely half of it once the bar is tall enough for that ratio to
    // beat its floor. Same floor as PoliceBar keeps the short desktop bar as it is.
    readonly property real  _barIconSize:     Math.max(12, root._valueSize * 0.85)
    readonly property real  _labelSize:       Math.max(11, root._statusHeight * 0.22)
    readonly property real  _valueSize:       PoliceBar.textSize
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
    readonly property color _warnColor:   "#ffb02e"

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
    // The distance flown since the arm, which is where Vehicle zeroes flightDistance: whole
    // metres, then kilometres to one place from 1 km. PoliceStatusPage writes its log the same way.
    readonly property string _flightDistanceText: {
        const metres = _activeVehicle ? Math.round(_activeVehicle.flightDistance.rawValue) : 0
        return metres >= 1000 ? (metres / 1000).toFixed(1) + " km" : metres + " m"
    }
    // blocked says the aircraft has refused to arm, which is the one state with something to go
    // and read: the banner grows a chevron and the drawer behind it lists the reasons above the
    // flight log. Carried on the object rather than derived from the text, so nothing has to
    // compare against a translated word.
    readonly property var _status: {
        if (!_activeVehicle) {
            return { text: qsTr("기체 연결 안 됨"), accent: _idleColor }
        }
        // Above armed, because every rung below reads a value the aircraft last sent and none of
        // them expires. With the link gone the block held whatever it had been - green 시동 가능
        // on a machine nobody can talk to, or blue 비행 중 with flightTime still counting up off
        // the ground station's own clock - which is the one reading that says a dead aircraft is
        // alive. What is on screen after this is a stale value with a red block over it saying so.
        if (_communicationLost) {
            return { text: qsTr("통신 두절"), accent: _alarmColor }
        }
        if (_activeVehicle.armed) {
            return { text: qsTr("비행 중 %1 %2").arg(_takeoffTime ? _flightElapsedText : "").arg(_flightDistanceText),
                     accent: _statusFlyColor }
        }
        const report = _activeVehicle.healthAndArmingCheckReport
        if (report && report.supported) {
            if (!report.canArm) {
                return { text: qsTr("시동 불가"), accent: _alarmColor, blocked: true }
            }
            return report.hasWarningsOrErrors
                ? { text: qsTr("주의"),      accent: _warnColor }
                : { text: qsTr("시동 가능"), accent: _statusOkColor }
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
                ? { text: qsTr("시동 가능"), accent: _statusOkColor }
                : { text: qsTr("시동 불가"), accent: _alarmColor, blocked: true }
        }
        return (_activeVehicle.allSensorsHealthy && _activeVehicle.autopilotPlugin
                && _activeVehicle.autopilotPlugin.setupComplete)
            ? { text: qsTr("시동 가능"), accent: _statusOkColor }
            : { text: qsTr("시동 불가"), accent: _alarmColor, blocked: true }
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
    readonly property color _idleColor:   "#8a9199"
    readonly property color _normalColor: "#e8edf2"
    readonly property color _alarmColor:  "#ff5b5b"

    signal menuRequested()

    // Camera window sizing and one-time docking along the bottom edge. Windows keep user
    // positions afterwards; a resize only re-clamps them through the drag axis limits.
    /// Which pod sensor the EO window shows. The pod's sub stream stays thermal either way,
    /// so the IR window is unaffected by this choice.
    ///
    /// Read off the routing the pod was last given rather than held as a flag of its own. The
    /// choice is made in the camera panel now, and a flag here would leave the window title
    /// naming one sensor while the window showed the other the moment it was used there.
    readonly property bool eoShowsWideAngle:
        App.SiyiCameraController.cameraImageType === 5 /* MainWideAngleSubThermal */

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
    //
    // Re-asserts the operator's own pick rather than a default, so a reconnect or an AI toggle
    // no longer drags the picture back off the wide-angle camera they chose in the camera panel.
    // The pod sends no readback of its routing, so what it was last told is the only record of
    // that pick there is, and it has to be re-sent: a pod that power-cycled came back in
    // whatever mode it booted in.
    function _applyPodStreams() {
        const wide = eoShowsWideAngle &&
                     !QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue
        App.SiyiCameraController.setCameraImageType(wide ? 5 : 3)
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

    // Fraction of the width the three windows had when they filled the right column top to
    // bottom. The size the owner picked off the previews.
    readonly property real _windowScale: 0.6

    // The zoom and thermal pair carry the pod's picture and the detections drawn on it, so they
    // are read from further away than the fixed forward camera and the owner sized them up on
    // their own. The forward window keeps _windowScale.
    readonly property real _stackWindowScale: 0.7

    // Height, not width, drives the 16:9 bodies: the band's height is what the map can spare.
    readonly property real _windowWidth: {
        const avail = height - topBar.height
        return avail * 16 / (9 * _windowCount) * _windowScale
    }

    readonly property real _stackWindowWidth: _windowWidth * _stackWindowScale / _windowScale
    readonly property real _stackWindowHeight: _stackWindowWidth * 9 / 16

    // The left edge of the zoom/thermal stack in the bottom right corner.
    readonly property real _columnX: width - _stackWindowWidth

    property bool _userMovedWindows: false

    function _dockWindows() {
        const bottom = height - _bottomInset
        // The instrument row keeps its first slot for the forward window (forwardSpacer), so it
        // lands on the row's own left inset and the compass, the telemetry bar and the detection
        // card slide right of it on their own.
        primaryWindow.x   = flightInstruments.x + forwardSpacer.x
        primaryWindow.y   = bottom - primaryWindow.height
        // Zoom over thermal in the bottom right corner, thermal on the forward window's own
        // baseline, so the three windows read as one band along the bottom edge.
        secondaryWindow.x = _columnX
        sharedWindow.x    = _columnX
        sharedWindow.y    = bottom - sharedWindow.height
        secondaryWindow.y = sharedWindow.y - secondaryWindow.height
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
    // The whole bar is PoliceTopBar.qml. Kept out of this file because it is a screen of its own
    // and because nothing else here depends on how it is drawn: what it needs is handed to it as
    // properties, so it resolves nothing up the scope chain and can be read on its own.
    //
    // id, objectName, anchors, height and z stay exactly as they were - everything below is
    // anchored to topBar.bottom.
    PoliceTopBar {
        id:            topBar
        objectName:    "policeTopBar"
        anchors.left:  parent.left
        anchors.right: parent.right
        anchors.top:   parent.top
        height:        root._statusHeight
        z:             4

        vehicle:        root._activeVehicle
        status:         root._status
        takeoffTime:    root._takeoffTime
        lowestBattery:  root._lowestBattery
        batteryPercent: root._batteryPercent
        batteryColor:   root._batteryColor
        rcAvailable:    root._rcAvailable
        rcLevel:        root._rcLevel
        linkUp:         root._linkUp
        gpsFixed:       root._gpsFixed
        gpsFixText:     root._gpsFixText

        iconSize:        root._barIconSize
        labelSize:       root._labelSize
        valueSize:       root._valueSize
        badgeSize:       root._badgeSize
        menuButtonWidth: root._menuButtonWidth
        labelColor:      root._labelColor
        barColor:        root._barColor
        alarmColor:      root._alarmColor
        warnColor:       root._warnColor
        idleColor:       root._idleColor

        gpsPage:           gpsDetailPage
        messagesPage:      vehicleMessagesPage
        linkSelectPage:    linkSelectPage
        linkConnectedPage: linkConnectedPage

        onMenuRequested: root.menuRequested()
    }

    // QGC's own parameter download progress, laid over the bar the way FlyViewToolBar lays it
    // over its own: a green line along the bar's bottom edge that grows with the download and is
    // gone once it is over. Here rather than inside the bar because it follows the active vehicle
    // by itself, and the bar reads nothing it was not handed. Same z and declared after, so it
    // draws over the bar and goes under whatever covers the bar.
    ParameterDownloadProgress {
        objectName:   "policeParamProgress"
        anchors.fill: topBar
        z:            topBar.z
    }

    // Obstacle glow over the map. One instance rather than one per map engine: the police layer
    // is built once whichever engine is loaded, and it is the only thing that knows where the map
    // is still visible - the top bar covers the map's top. No right margin: the camera stack
    // covers only the bottom right corner, and a margin that cleared it would leave the
    // right-hand band floating a window's width inside the map for the whole of the rest.
    // z below every sibling so the tool strips and the instruments keep reading over it.
    PoliceDroneObstacleGlow {
        id:                  obstacleGlow
        anchors.left:        parent.left
        anchors.right:       parent.right
        anchors.top:         topBar.bottom
        anchors.bottom:      parent.bottom
        z:                   -1

        // The forward number sits under the top bar, which is also where the confirm control and
        // the two banners drop in; all three are above this glow and would bury it. The lowest of
        // whichever are up, in this item's own coordinates. The confirm host is already anchored
        // below the banners, so its term covers those as well when it is up.
        topLabelInset: Math.max(linkLostBanner.visible
                                    ? linkLostBanner.y + linkLostBanner.height - obstacleGlow.y : 0,
                                followModeBanner.visible
                                    ? followModeBanner.y + followModeBanner.height - obstacleGlow.y : 0,
                                guidedConfirmHost.contentBottom > 0
                                    ? guidedConfirmHost.y + guidedConfirmHost.contentBottom - obstacleGlow.y : 0)
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
    // drag-to-track gesture, and a short tap on the video toggles fullscreen.

    component CameraWindow : Item {
        id: win

        property string title
        property string detail
        property string panelKey
        property alias slot: contentSlot
        /// The forward window and the zoom/thermal stack are at two different scales.
        property real bodyWidth: root._windowWidth

        width:  bodyWidth
        height: bodyWidth * 9 / 16
        // Tapping a camera fills the screen with it. The other two windows go with it: they sit
        // over the picture the operator zoomed in to see. Only the map stays, small, in the
        // corner.
        visible: root.expandedPanel.length === 0
        z:       10

        Rectangle {
            anchors.fill: parent
            radius:       0
            color:        root._panelColor

            // Stops a tap or drag on the window from also reaching the map underneath,
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
            // Read by the layout test, which holds the state chips clear of this one. Every
            // window carries the same name; the test looks it up inside one window's subtree.
            objectName:         "cameraWindowTitleChip"
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
        id:         primaryWindow
        // Read by the layout test, which checks where the band's three windows landed.
        objectName: "cameraWindowPrimary"
        panelKey:   "primary"
        title:      qsTr("전방")
        // Detail text carries a value or a warning, never wiring trivia the operator
        // cannot act on.
        detail:     root._fpvConfigured ? "" : qsTr("주소 미설정")
    }

    CameraWindow {
        id:         secondaryWindow
        objectName: "cameraWindowSecondary"
        panelKey:   "secondary"
        bodyWidth:  root._stackWindowWidth
        // Operator words, not industry ones: zoom / wide / thermal, never EO or IR.
        title:      root._aiStreamActive ? qsTr("AI 인식")
                                         : (root.eoShowsWideAngle ? qsTr("광각") : qsTr("줌"))
    }

    CameraWindow {
        id:         sharedWindow
        objectName: "cameraWindowShared"
        panelKey:   "shared"
        bodyWidth:  root._stackWindowWidth
        title:      qsTr("열상")
        // At night thermal and the laser rangefinder work as a pair, so the distance lives
        // on this window's bar once readings arrive. No reading, no text.
        detail:     App.SiyiCameraController.rangefinderAvailable
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
            // ToolStripHoverButton takes its objectName from the action's, so these two names
            // reach the strip buttons themselves.
            GuidedActionTakeoff            { text: qsTr("이륙"); objectName: "policeToolTakeoff" },
            // Mission start on the strip itself, alongside takeoff. Stock also keeps this action
            // inside the 동작 drop panel; showStartMission puts it here once a route is aboard and
            // takes it away again in flight.
            GuidedToolStripAction {
                objectName: "policeToolStartMission"
                text:       qsTr("미션시작")
                iconSource: "qrc:/qmlimages/Plan.svg"
                actionID:   _guidedController.actionStartMission
                visible:    _guidedController.showStartMission
            },
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
        objectName:         "policeGuidedToolStrip"
        anchors.left:       parent.left
        anchors.leftMargin: 8
        anchors.top:        topBar.bottom
        anchors.topMargin:  8
        z:                  4
        // ToolStrip's own default, which still clears four Korean characters at the strip's
        // small font. The buttons size themselves to their labels inside it.
        width:              ScreenTools.defaultFontPixelWidth * 7
        // Stops a gap above the forward window rather than at the bottom inset: with every entry
        // showing, a strip measured to the bottom edge ran straight over that window at tablet
        // scale. ToolStrip scrolls whatever does not fit inside maxHeight.
        maxHeight:          root.height - root._bottomInset - (root._windowWidth * 9 / 16) - 8 - y
        model:              policeToolActions.model
    }

    // The pod's controls: camera panel, EO sensor, zoom, shutter, AI tracking and the
    // loudspeaker. The strip's own drop panel only opens to the right, which here is the
    // screen edge, so the panels are DropPanels opened by hand and told the map is the
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
            // Wide angle, 20x and recentre used to sit here. The camera panel above carries all
            // three - the sensor combination, the zoom pair with its magnification readout, and
            // its own 중앙 on the same center() call - so the strip is down to the things that
            // are not in it. Zoom there is stepped rather than the strip's one tap to 20x.
            // One shutter for both, the way a camera has one: a press takes the photo, a press
            // held starts and stops the video. Two buttons for it cost two of the strip's rungs
            // and put the operator's thumb one rung away from the wrong one; a stills camera has
            // taught everyone which button takes a picture, and it is this shape.
            //
            // The label follows the pod's own recording state rather than the request, so a start
            // that never took does not sit here reading as recording, and the glyph carries the
            // state too - ring and dot for ready, ring and square for stop.
            ToolStripAction {
                // Named for what it will do, including when that is nothing: the pod refuses both
                // a photo and a recording with no card in it and reports nothing back beyond the
                // refusal, so left enabled this button answered a press with silence - which
                // reads as a broken control rather than as a missing card.
                text:        App.SiyiCameraController.noSdCard  ? qsTr("SD카드 없음")
                             : App.SiyiCameraController.recording ? qsTr("녹화중지")
                                                                  : qsTr("촬영")
                iconSource:  App.SiyiCameraController.recording ? "/res/police_shutter_recording.svg"
                                                                : "/res/police_shutter.svg"
                fullColorIcon: true
                enabled:     App.SiyiCameraController.connected &&
                             !App.SiyiCameraController.noSdCard
                onTriggered: App.SiyiCameraController.takePhoto()
                onHeldDown:  App.SiyiCameraController.toggleRecording()
            },
            ToolStripAction {
                // The pod's AI module. The highlight follows the operator's request as well as the
                // module's answer: a press has to light the button at once or the control reads as
                // dead while the module is still answering. The request is dropped once the module
                // disagrees for good (see _aiRequested), so a refusal still stops reading as armed.
                text:        qsTr("AI")
                iconSource:  "/res/police_ai.svg"
                checkable:   true
                checked:     root._aiRequested !== null ? root._aiRequested
                                                       : App.SiyiAiController.recognitionEnabled
                onTriggered: {
                    // The strip button is a checkable Button: it flips its own checked state and
                    // writes it through to this action before triggering, so checked is already
                    // the state the press is asking for. Negating it commanded the opposite one
                    // and the rebinding below snapped the highlight straight back off.
                    root._setAiEnabled(checked)
                    // The strip button owns its own checked state once pressed, which drops
                    // the binding above; put it back so the highlight keeps following.
                    checked = Qt.binding(() => root._aiRequested !== null
                                                   ? root._aiRequested
                                                   : App.SiyiAiController.recognitionEnabled)
                }
            },
            ToolStripAction {
                // The face mosaic, which is all the on-device detector does now. Its own switch
                // rather than a rider on AI above: it is a personal-data measure that outlives any
                // recognition setting, and it is the one control here that costs about 100 ms of
                // tablet CPU per frame, so the operator needs to be able to drop it on its own.
                //
                // One label in every state. A strip label neither wraps nor elides, so 모자이크 불가
                // came out clipped at both ends; the state is carried by the button instead, the
                // way the AI entry above carries its own.
                //
                // Three states, not two. The switch being on does not mean anything is being
                // mosaicked: a model that failed to load leaves active false with enabled true and
                // the overlay draws nothing, so that case is greyed out rather than lit. active is
                // the detector's own answer, enabled only the operator's request.
                objectName:  "policeMosaicToolAction"
                text:        qsTr("모자이크")
                iconSource:  App.PersonDetector.active ? "qrc:/InstrumentValueIcons/view-show.svg"
                                                       : "qrc:/InstrumentValueIcons/view-hide.svg"
                checked:     App.PersonDetector.enabled && App.PersonDetector.active
                enabled:     !App.PersonDetector.enabled || App.PersonDetector.active
                onTriggered: {
                    App.PersonDetector.enabled = !App.PersonDetector.enabled
                    // The strip button echoes its own checked back into this action, which drops
                    // the binding above the first time the detector answers; put it back so the
                    // lit state keeps following the detector, the way the AI entry does.
                    checked = Qt.binding(() => App.PersonDetector.enabled && App.PersonDetector.active)
                }
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

    // The stock altitude slider takes the whole right edge while a takeoff, land or pause
    // confirmation is up, and the camera strip lives on that edge. The strip steps inboard of the
    // slider for as long as it is there rather than being covered by it. Reached off the context
    // chain, the way PoliceGuidedConfirmHost reaches the same control.
    readonly property var  _altitudeSlider: globals.guidedControllerFlyView
                                                ? globals.guidedControllerFlyView.guidedValueSlider
                                                : null
    // Right edge the strip and its handle sit against, inboard of the slider while it is up.
    readonly property real _cameraStripRight:
        width - ((_altitudeSlider && _altitudeSlider.visible) ? _altitudeSlider.width : 0) - 8

    // Two columns rather than the stock strip's one: six entries in a single column ran past
    // the zoom window at tablet font metrics and the last of them had to be scrolled for.
    PoliceCameraToolGrid {
        id:                 cameraToolStrip
        objectName:         "policeCameraToolStrip"
        x:                  root._cameraStripRight - width
        anchors.top:        topBar.bottom
        anchors.topMargin:  8
        // Under the left strip's click-away layer (its z - 1), so a tap here while the
        // 복귀고도 panel is open closes that panel instead of firing a camera command.
        z:                  toolStrip.z - 2
        // Folded away by the handle beside it. It is a block of buttons in the middle of the
        // screen and the map beneath it is what the operator reads between camera actions, so it
        // gets out of the way the same way the plan view's mission panel does.
        visible:            root.cameraStripOpen
        // The strip shares the right edge with the zoom/thermal stack below it, so it stops above
        // the zoom window's top. It also stops above the detection card where the card is actually
        // underneath - on a screen wide enough for the card to sit clear of this column, deferring
        // to it anyway clipped the top of the strip off. The lower of the two floors wins.
        // Counted off the dock arithmetic, not off the windows, which the operator can drag away.
        // A safety floor only: at two columns all six entries fit well inside it, and whatever
        // would not fit scrolls, as it does in the stock strip.
        maxHeight: {
            const clearsCard = (aiPanel.x >= x + width) || (aiPanel.x + aiPanel.width <= x)
            const cardFloor = clearsCard ? (root.height - root._bottomInset) : (aiPanel.y - 8)
            const stackFloor = root.height - root._bottomInset - root._stackWindowHeight * 2 - 8
            return Math.min(cardFloor, stackFloor) - y
        }
        model:              cameraToolActions.model
    }

    // The strip's handle, and the only part of it that stays when it is folded. Same ground as
    // the strip so open they read as one piece, and its own touch floor so it can still be hit
    // with a glove. Kept at the strip's top rather than its middle: the strip's height changes
    // with the detection card under it, and a handle that moved with it would be somewhere new
    // every time the operator reached for it.
    Rectangle {
        id:     cameraStripHandle
        width:  Math.max(ScreenTools.defaultFontPixelWidth * 2.6, ScreenTools.minTouchPixels * 0.55)
        height: Math.max(ScreenTools.minTouchPixels, ScreenTools.defaultFontPixelHeight * 2.2)
        x:      root.cameraStripOpen ? cameraToolStrip.x - width - 4
                                     : root._cameraStripRight - width
        y:      cameraToolStrip.y
        z:      cameraToolStrip.z
        radius: ScreenTools.defaultFontPixelWidth / 2
        color:  Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b,
                        cameraStripHandleArea.pressed ? 0.95 : 0.75)

        Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }

        QGCColoredImage {
            anchors.centerIn: parent
            width:            ScreenTools.defaultFontPixelHeight
            height:           width
            source:           root.cameraStripOpen ? "/res/chevron-double-right.svg"
                                                   : "/res/chevron-double-left.svg"
            color:            qgcPal.text
        }

        QGCMouseArea {
            id:           cameraStripHandleArea
            anchors.fill: parent
            onClicked:    root.cameraStripOpen = !root.cameraStripOpen
        }
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

    // The compass pill is the third thing in that corner and it sizes itself off the window,
    // which left it a good deal shorter than the two storeys beside it. HorizontalCompassAttitude
    // takes width as its only input and derives the rest from it - its _topBottomMargin,
    // _innerRadius and _outerRadius chain comes out at height = 0.5125 * width - so the height
    // the corner wants is asked for as the width that produces it. Implicit heights only: the
    // resolved ones run back through the bar's width and into this.
    readonly property real _compassHeightPerWidth: 0.5125
    readonly property real _cornerStackHeight:     aiPanel.implicitHeight + 6 + telemetryBar.implicitHeight
    // A notch short of the two rows beside it, card top to bar bottom. Matched to them exactly
    // the pill was the tallest thing on the bottom edge; the height it gives up here is room the
    // two rows get back at their left end, and the dial still reads.
    readonly property real _instrumentHeightOfStack: 0.85
    // How much of the panel's own window colour the pill's background keeps.
    readonly property real _instrumentBackgroundAlpha: 0.5
    // The pill takes whatever width that height needs at its own proportions. No clamp against
    // the room the rows want: clamped to leave the bar its width the pill came out half size on
    // the tablet with its numbers on top of each other. The instruments win and the rows give
    // way - what stops short instead is the camera tool strip above them, through its maxHeight.
    readonly property real _instrumentWidth:
        (_cornerStackHeight * _instrumentHeightOfStack) / _compassHeightPerWidth

    // The instrument row along the bottom left: the forward camera's slot at the edge, then
    // attitude and compass, then the telemetry values. QGC's FlyViewBottomRightRowLayout puts
    // the values first and slides their background under the instrument pill, a seam that
    // only works in that order, so the row is assembled here rather than reused.
    RowLayout {
        id:                   flightInstruments
        objectName:           "policeInstrumentRow"
        anchors.left:         parent.left
        anchors.leftMargin:   8
        anchors.bottom:       parent.bottom
        anchors.bottomMargin: root._bottomInset
        spacing:              6
        // Below the camera windows (z 10) so a window dragged this way passes over the
        // instruments instead of disappearing behind them.
        z:                    3

        // The forward window's slot, first in the row so the window stands at the row's own left
        // inset. It holds the space; the window itself is drawn over it, so the instruments and
        // everything after them in the row move along on their own. Height 1 so the window's own
        // height does not push the row up off the bottom edge.
        Item {
            id:                     forwardSpacer
            Layout.preferredWidth:  root._windowWidth
            Layout.preferredHeight: 1
            Layout.alignment:       Qt.AlignBottom
            // The row settles after load, and the forward window is docked off this slot.
            onXChanged:             root._redockIfPristine()
        }

        FlyViewInstrumentPanel {
            id:               instrumentPanel
            objectName:       "policeInstrumentPanel"
            Layout.alignment: Qt.AlignBottom
            visible:          QGroundControl.corePlugin.options.flyView.showInstrumentPanel
                              && root._showSingleVehicleUI

            // Only the horizontal style is driven this way. The other two the operator can pick
            // lay their compass and horizon out differently, and a width chosen for this one's
            // arithmetic would be a guess against theirs.
            Binding {
                target:      instrumentPanel.innerControl
                property:    "width"
                value:       root._instrumentWidth
                when:        (instrumentPanel.innerControl !== null) &&
                             QGroundControl.settingsManager.flyViewSettings.instrumentQmlFile2
                                 .rawValue.endsWith("HorizontalCompassAttitude.qml")
                restoreMode: Binding.RestoreBindingOrValue
            }

            // The map reads through the pill's background while everything drawn on it stays
            // solid. The background is the stock widget's own root Rectangle, so its colour is
            // overridden here at half alpha; opacity on the pill would take the attitude ball,
            // the dial, the numbers and the proximity ring down with it. Nothing happens on an
            // instrument variant that has no colour of its own.
            Binding {
                target:      instrumentPanel.innerControl
                property:    "color"
                value:       Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b,
                                     root._instrumentBackgroundAlpha)
                when:        (instrumentPanel.innerControl !== null) &&
                             (instrumentPanel.innerControl.color !== undefined)
                restoreMode: Binding.RestoreBindingOrValue
            }

            // Around the compass dial, which is the pill's right-hand rounded end: that end's arc
            // centre is the dial's centre, so the pill's own width and height place the ring and
            // the shared compass widget stays untouched. Only for the horizontal instrument, for
            // the same reason its width is only driven there -
            // the other two put their compass somewhere else entirely.
            PoliceDroneProximityRing {
                active:        QGroundControl.settingsManager.flyViewSettings.instrumentQmlFile2
                                   .rawValue.endsWith("HorizontalCompassAttitude.qml")
                // One forward lidar, so every lidar display keeps the nose at the top, like the map
                // band and the video ring: the arc stays at 12 o'clock whatever the dial does.
                northUp:       false
                // The pill's rim is the ring's outer bound and the dial face its inner one. All the
                // room there is between them is the margin HorizontalCompassAttitude keeps outside
                // the dial, (width * 0.05) / 2; a band wider than that either buries the dial's
                // ticks or paints on the attitude ball and the map either side of the pill.
                ringRadius:    instrumentPanel.innerControl ? instrumentPanel.innerControl.height / 2 : 0
                boldStroke:    instrumentPanel.innerControl ? instrumentPanel.innerControl.width * 0.025 : 0
                warnStroke:    boldStroke * 0.7
                // Nothing bright under these arcs to outline them against, and the outline would
                // take half of the margin they have to fit in.
                outlined:      false
                x:             instrumentPanel.innerControl
                                   ? instrumentPanel.innerControl.width - instrumentPanel.innerControl.height / 2 - (width / 2)
                                   : 0
                y:             instrumentPanel.innerControl ? (instrumentPanel.innerControl.height - height) / 2 : 0
                showQuietRing: true
            }
        }

        TelemetryValuesBar {
            id:                     telemetryBar
            // Read by the layout test.
            objectName:             "policeTelemetryBar"
            Layout.alignment:       Qt.AlignBottom
            // The card stacks on this bar and the two share one width, so the bottom band reads
            // as one block rather than two ragged strips. The wider of the two contents sets it
            // and nothing else does: stretched to a screen inset instead, the block was wider
            // than anything in it and the values pooled in the middle of it.
            Layout.preferredWidth:  Math.max(implicitWidth, aiPanel.implicitWidth)
            settingsGroup:          factValueGrid.telemetryBarSettingsGroup
            specificVehicleForCard: null // Tracks the active vehicle
        }
    }

    // ------------------------------------------------------------------ map scale
    //
    // QGC's own map scale, which stock hosts in FlyViewWidgetLayer and so went with it; the
    // picture-in-picture one inside FlyViewMap was never lost. Same visibility rule as stock.
    // Bottom left, a stock margin above the forward window's dock and the detection card, and
    // right of the tool strip and the lidar glow's left band. Counted off the dock arithmetic,
    // not off the forward window, which the operator can drag away.
    MapScale {
        // Read by the layout test.
        objectName: "policeMapScale"
        x:          Math.max(toolStrip.x + toolStrip.width, obstacleGlow._thickness) + root._toolsMargin
        y:          Math.min(aiPanel.y, root.height - root._bottomInset - root._windowWidth * 9 / 16)
                        - height - root._toolsMargin
        mapControl: root.mapItem
        autoHide:   true
        visible:    !ScreenTools.isTinyScreen && QGroundControl.corePlugin.options.flyView.showMapScale && QGCViewer3DManager.displayMode !== QGCViewer3DManager.View3D && !!root.mapItem && root.mapItem.pipState.state === root.mapItem.pipState.fullState &&
                    (!root.mapItem.geoMap || (root.mapItem.geoMap.camera.mode === GeoMapCamera.Mode2D && root.mapItem.geoMap.camera.isTopDown))
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
            // Read by the layout test.
            objectName: "policeFullscreenMapPip"
            // Off the screen width, not the camera band's: this is the only map on screen while
            // a camera is full, and a fraction of a window that the owner keeps resizing left it
            // too small to read a position off.
            width:   parent.width * 0.27
            height:  width * 9 / 16
            x:       parent.width - width - 8
            y:       parent.height - height - 8
            z:       3
            visible: root.mapItem !== null

            // Only a backdrop for the frame or two before the mirror has a texture: no border,
            // no inset, and square, since rounding it would need a mask over the mirror that
            // the software backend does not draw, leaving corners peeking out behind the copy.
            Rectangle {
                anchors.fill: parent
                color:        root._panelColor
            }

            // No title bar here. It is visibly a map, it cannot be dragged (it sits in the
            // swapped window's slot), and the bar only shrank the tap target - the whole
            // point of this thing is to be tapped.
            ShaderEffectSource {
                id:           mapMirror
                anchors.fill: parent
                sourceItem:   root.mapItem
                live:         true
                textureSize:  Qt.size(Math.round(width * 2), Math.round(height * 2))
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

        // How to get back out, said once on the way in and then gone. It is the same two
        // gestures every time, so after the first few seconds it is a caption printed over the
        // picture the operator opened this view to look at. Faded rather than cut so the eye
        // is not pulled back to it as it goes.
        Rectangle {
            id:                   hintChip
            // Read by the layout test, which holds the centred detection card clear of it.
            objectName:           "policeFullscreenHint"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom:       parent.bottom
            // Above the detection card, which floats over this layer.
            anchors.bottomMargin: root._bottomInset + aiPanel.height + 12
            width:           fullscreenHint.implicitWidth + 20
            height:          fullscreenHint.implicitHeight + 12
            radius:          4
            color:           "#c0121b24"
            z:               2
            opacity:         0
            visible:         opacity > 0

            Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.InOutQuad } }

            // Restarted on every entry, not only the first: the layer is not destroyed between
            // them, so a timer left run out would leave the hint hidden for the rest of the
            // session and the gesture unlearnable for whoever picks the controller up next.
            Timer {
                id:          hintTimer
                interval:    3000
                onTriggered: hintChip.opacity = 0
            }

            Connections {
                target: root

                function onExpandedPanelChanged() {
                    if (root.expandedPanel.length > 0) {
                        hintChip.opacity = 1
                        hintTimer.restart()
                    } else {
                        hintTimer.stop()
                        hintChip.opacity = 0
                    }
                }
            }

            Text {
                id:             fullscreenHint
                anchors.centerIn: parent
                color:          "white"
                font.pixelSize: Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                text:           qsTr("화면 터치 또는 뒤로가기: 분할화면")
            }
        }
    }

    // Detection strip stacked on the telemetry bar, so the bottom band is two rows deep right of
    // the compass. Floats above the full screen layer the way the windows do.
    PoliceDroneAiPanel {
        id: aiPanel

        // Read by the layout test.
        objectName: "policeAiPanel"

        // The card starts exactly where the telemetry bar does: pulling it left to fit a wider
        // card would slide it over the attitude and compass beside them.
        // Full screen the map in the right corner takes a quarter of the width, so the counts go
        // to the middle of the bottom edge instead of into the gap beside it.
        x:      root.expandedPanel.length > 0
                    ? (root.width - width) / 2
                    : flightInstruments.x + telemetryBar.x
        // Full screen there is no telemetry bar under it to sit on - the instruments go with the
        // map - so the card was left hanging one bar's height above the bottom edge, over the
        // picture, with the fullscreen hint drawn through it. Against the bottom instead, which
        // is where the hint already expects to find it.
        y:      root.expandedPanel.length > 0
                    ? root.height - root._bottomInset - height
                    : flightInstruments.y + telemetryBar.y - 6 - height
        // One width with the bar under it, which the bar resolves from the wider of the two.
        // Full screen there is no bar on screen to match, so the card keeps its own width.
        width:  root.expandedPanel.length > 0 ? implicitWidth : telemetryBar.width
        height: implicitHeight
        z:      root.expandedPanel.length > 0 ? 21 : 3
    }

    // The forward-looking camera on the air unit's second LAN port. It is fixed to the
    // airframe, so it takes no AI target picking.
    PoliceDroneCameraPanel {
        id:                   primaryPanel
        parent:               root.expandedPanel === "primary" ? fullscreenLayer : primaryWindow.slot
        anchors.fill:         parent
        panelTitle:           qsTr("전방")
        showChrome:           root.expandedPanel === "primary"
        streamObjectName:     "fpvVideo"
        proximityRingEnabled: true
        onActivated:          root._toggleExpanded("primary")
    }

    PoliceDroneCameraPanel {
        id:                   secondaryPanel
        // Read by the chip test, which drives follow and tracking on this panel.
        objectName:           "policeZoomCameraPanel"
        parent:               root.expandedPanel === "secondary" ? fullscreenLayer : secondaryWindow.slot
        anchors.fill:         parent
        // Named for the sensor on screen, not the pipe it came through: AI pins the main
        // stream to the zoom camera, so its feed is the zoom picture with boxes drawn in.
        panelTitle:           root._aiStreamActive ? qsTr("줌 · AI")
                                                    : (root.eoShowsWideAngle ? qsTr("광각") : qsTr("줌"))
        showChrome:           root.expandedPanel === "secondary"
        streamObjectName:     "videoContent"
        personDetectionEnabled: true
        aiTargetVisible:      root.aiTargetVisible
        aiTargetX:            root.aiTargetX
        aiTargetY:            root.aiTargetY
        aiTargetWidth:        root.aiTargetWidth
        aiTargetHeight:       root.aiTargetHeight
        aiTargetLabel:        root.aiTargetLabel
        aiTargetInfo:         root.aiTargetInfo
        // A target the module has lost is not one it is following: it keeps hasTarget up while
        // it hunts for the object again, and a green chip through that is a claim nothing backs.
        trackingActive:       App.SiyiAiController.hasTarget && !App.SiyiAiController.targetLost
        // Same rule for follow. 0xC3 answers only when asked and there is no way to ask again
        // without re-asserting follow, so a lit chip from a minutes-old reply would be a claim
        // nothing backs either - a stale one goes grey with the rest.
        followActive:         App.SiyiCameraController.aiFollowEnabled &&
                              !App.SiyiCameraController.aiFollowStale
        targetPickEnabled:    root._aiPickEnabled
        // The camera rail's 추적해제 is behind the picture full screen, which is where the
        // operator is drawing boxes; this panel carries its own.
        trackCancelEnabled:   App.SiyiAiController.hasTarget
        onActivated:          root._toggleExpanded("secondary")
        onTargetBoxPicked:    (l, t, r, b) => App.SiyiAiController.trackBox(l, t, r, b)
        onTrackCancelRequested: App.SiyiAiController.cancelTracking()
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

    // The slide-to-confirm control every guided action ends at. Top centre under the bar, and
    // below a warning banner while one is up rather than over it. z clears every other layer
    // here, the fullscreen camera at 20 and the track toast at 30 included, so a confirmation
    // the operator asked for is never buried.
    PoliceGuidedConfirmHost {
        id:                       guidedConfirmHost
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top:              followModeBanner.visible
                                      ? followModeBanner.bottom
                                      : (linkLostBanner.visible ? linkLostBanner.bottom : topBar.bottom)
        anchors.topMargin:        ScreenTools.defaultFontPixelHeight / 2
        z:                        40
    }

}
