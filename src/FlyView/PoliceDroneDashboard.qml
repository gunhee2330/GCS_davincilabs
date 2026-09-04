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

    // MainStatusIndicator assigns its own background tint while computing the status label and
    // reads the link state, both off the QML context chain that FlyViewToolBar provides. Without
    // them the assignment throws "Invalid write to global property", the label function aborts
    // part-way, and the indicator renders as an empty zero-width item.
    property color _mainStatusBGColor: qgcPal.brandingPurple
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
    // The top bar hosts QGC's own toolbar indicators, which are laid out against
    // ScreenTools.toolbarHeight; anything shorter clips them.
    readonly property real _statusHeight: Math.max(ScreenTools.minTouchPixels * 1.2, ScreenTools.toolbarHeight)
    readonly property color _panelColor:  "#e5121b24"
    /// Gap kept clear along the bottom edge now that the control panel floats rather than
    /// occupying two full-width bars.
    readonly property real _bottomInset:  8
    readonly property color _accentColor: "#33c7ff"

    signal menuRequested()

    // Camera window sizing and one-time docking along the bottom edge. Windows keep user
    // positions afterwards; a resize only re-clamps them through the drag axis limits.
    /// True while the pod is configured for ZT30 image mode 2 (main stream = zoom | wide
    /// side by side, sub stream = thermal), which is what feeds three distinct sensors into
    /// the three camera windows.
    property bool splitMainStream: false

    /// The AI module's own RTSP feed replaces the pod sub stream in the third window when it
    /// is configured, so the panel labels itself accordingly.
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
                root._applyTripleView()
            }
        }
    }

    // Image mode 2 packs zoom|wide side by side on the main stream, which feeds all three
    // sensors to the three windows. The AI module infers on that same main stream and its
    // manual requires it to be the zoom camera, so a composite would hand the detector a
    // doubled image. When AI is in use the pod drops to mode 3 (main = zoom at full width)
    // and the wide-angle window goes dark - detection wins over the third view.
    readonly property bool _aiNeedsFullZoomMain:
        QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue

    function _applyTripleView() {
        if (_aiNeedsFullZoomMain) {
            App.SiyiCameraController.setCameraImageType(3)
            splitMainStream = false
        } else {
            App.SiyiCameraController.setCameraImageType(2)
            splitMainStream = true
        }
    }

    // Re-apply when AI is switched on or off so the pod layout follows the setting.
    // Watched through the Fact rather than a handler on _aiNeedsFullZoomMain: a readonly
    // property takes no onChanged handler.
    Connections {
        target: QGroundControl.settingsManager.siyiCameraSettings.aiEnabled
        function onRawValueChanged() {
            if (App.SiyiCameraController.connected) {
                root._applyTripleView()
            }
        }
    }

    readonly property real _gripHeight: Math.max(ScreenTools.minTouchPixels * 0.7, ScreenTools.defaultFontPixelHeight * 1.2)

    /// The FPV window only exists once an address is set, so window count is not fixed.
    readonly property bool _fpvConfigured:
        QGroundControl.settingsManager.siyiCameraSettings.fpvRtspUrl.rawValue.trim() !== ""

    readonly property int _windowCount: _fpvConfigured ? 4 : 3

    // The windows stack down the right edge, so the width driving their 16:9 bodies is bounded
    // by the height left between the top bar and the control panel below them — sized on width
    // alone the last window would run off the bottom. Adding the FPV window narrows all of them.
    readonly property real _windowWidth: {
        const gap = 8
        // The right edge belongs to the windows alone now that the controls live in the tool
        // strip, so the column may use the full height between the top bar and the bottom
        // inset.
        const avail = height - _bottomInset - topBar.height - gap * (_windowCount + 1)
        const byHeight = (avail - _gripHeight * _windowCount) * 16 / (9 * _windowCount)
        return Math.max(ScreenTools.minTouchPixels * 2.5, Math.min(width * 0.23, ScreenTools.defaultFontPixelWidth * 26, byHeight))
    }

    property bool _userMovedWindows: false

    function _dockWindows() {
        const gap = 8
        const windows = [primaryWindow, secondaryWindow, sharedWindow, fpvWindow]
        const xDock = width - _windowWidth - gap
        let y = topBar.height + gap
        for (let i = 0; i < windows.length; ++i) {
            if (!windows[i].visible) {
                continue
            }
            windows[i].x = xDock
            windows[i].y = y
            y += windows[i].height + gap
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
            _applyTripleView()
        }
    }

    Connections {
        target: root
        function onHeightChanged() { root._redockIfPristine() }
    }

    function _factText(fact, fallback) {
        return fact ? fact.valueString + (fact.units.length > 0 ? " " + fact.units : "") : fallback
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
        _takeoffTime ? Qt.formatDateTime(_takeoffTime, "yyyy-MM-dd HH:mm:ss") : qsTr("이륙 전")

    // flightTime is registered on the fact group but, unlike its sibling flightDistance, has
    // no Q_PROPERTY accessor — so vehicle.flightTime is undefined and only the name lookup
    // reaches it. Bound once per vehicle rather than called from the delegate binding.
    readonly property var _flightTimeFact: _activeVehicle ? _activeVehicle.getFact("flightTime") : null

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

    Rectangle {
        id: topBar
        anchors.left:  parent.left
        anchors.right: parent.right
        anchors.top:   parent.top
        height:        root._statusHeight
        color:         root._panelColor
        z:             4

        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill:        parent
            anchors.leftMargin:  ScreenTools.defaultFontPixelWidth * 0.6
            anchors.rightMargin: ScreenTools.defaultFontPixelWidth
            spacing:             ScreenTools.defaultFontPixelWidth * 0.7

            // A plain Button paints the style's own opaque background, which read as a white
            // slab on this dark bar. Transparent background plus an explicitly light icon
            // matches how the toolbars in the other views render theirs.
            // The icon is sized explicitly rather than filling the button: as the content item
            // it stretched to the bar height and came out larger than the ☰ elsewhere.
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
                Layout.preferredHeight: root._statusHeight * 0.4
                Layout.preferredWidth:  Layout.preferredHeight * (1153 / 122)
                source:                 "/res/DavinciLabsLogo.png"
                fillMode:               Image.PreserveAspectFit
                smooth:                 true
            }

            // QGC's own status indicators rather than a hand-rolled subset: these carry the
            // arming/health state and open detail popups on click (satellite counts, per-cell
            // battery, RC and telemetry signal), which a row of labels cannot do. Altitude and
            // ground speed are deliberately absent — the telemetry bar bottom right owns those.
            // Carries the status tint the indicator computes — red on comms lost, green when
            // ready, yellow on a warning — the way the stock toolbar's gradient does.
            // Only with a vehicle: without one the indicator offers "click to manually
            // connect", and the plug at the bar's right end is the one connect control now.
            Rectangle {
                Layout.fillHeight:     true
                Layout.preferredWidth: mainStatus.implicitWidth + ScreenTools.defaultFontPixelWidth * 2
                visible:               root._activeVehicle
                color:                 root._mainStatusBGColor
                opacity:               0.55
                radius:                4

                MainStatusIndicator {
                    id:               mainStatus
                    objectName:       "toolbar_mainStatusIndicator"
                    anchors.centerIn: parent
                    height:           parent.height
                }
            }

            FlightModeIndicator {
                objectName:        "toolbar_flightModeIndicator"
                Layout.fillHeight: true
                visible:           root._activeVehicle
            }

            Item { Layout.fillWidth: true }

            // Flight log: takeoff instant, elapsed time, distance flown and the airframe's
            // takeoff count. Elides rather than squeezing the indicators when the window is
            // narrow.
            RowLayout {
                id:                    flightLog
                Layout.fillHeight:     true
                Layout.maximumWidth:   implicitWidth
                spacing:               ScreenTools.defaultFontPixelWidth
                visible:               root._activeVehicle && root.width > ScreenTools.defaultFontPixelWidth * 105

                Repeater {
                    model: [
                        { label: qsTr("이륙"), value: root._takeoffText },
                        { label: qsTr("비행"), value: root._flightTimeFact
                                                          ? root._flightTimeFact.valueString
                                                          : "--" },
                        { label: qsTr("이동"), value: root._factText(root._activeVehicle
                                                          ? root._activeVehicle.flightDistance : null, "--") },
                        { label: qsTr("이륙 횟수"), value: qsTr("%1회").arg(App.TakeoffCounter.takeoffCount) }
                    ]

                    delegate: Row {
                        id: logItem
                        required property var modelData
                        Layout.alignment: Qt.AlignVCenter
                        spacing:          4

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  root._accentColor
                            font.pixelSize:         Math.max(11, ScreenTools.defaultFontPixelHeight * 0.68)
                            text:                   logItem.modelData.label
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color:                  "white"
                            font.pixelSize:         Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                            // Monospace keeps the clock and counters from shifting the row
                            // sideways as digits change.
                            font.family:            "Consolas, monospace"
                            text:                   logItem.modelData.value
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            FlyViewToolBarIndicators {
                Layout.fillHeight:     true
                Layout.preferredWidth: implicitWidth
            }

            // AI module state, just left of the link plug: the chip icon and AI ON / AI OFF,
            // keyed on the tracking module answering on its socket.
            Row {
                Layout.fillHeight: true
                Layout.alignment:  Qt.AlignVCenter
                spacing:           6

                readonly property bool aiUp: App.SiyiAiController.connected
                readonly property color tint: aiUp ? "#42d66b" : "#ff9c46"

                QGCColoredImage {
                    anchors.verticalCenter: parent.verticalCenter
                    width:                  root._menuIconSize * 1.15
                    height:                 width
                    source:                 "/res/police_ai.svg"
                    color:                  parent.tint
                    fillMode:               Image.PreserveAspectFit
                    sourceSize.height:      height
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color:                  parent.tint
                    font.bold:              true
                    font.pixelSize:         Math.max(12, ScreenTools.defaultFontPixelHeight * 0.75)
                    text:                   parent.aiUp ? qsTr("AI ON") : qsTr("AI OFF")
                }
            }

            // Link state at the far right: a plug in or out of its socket, with the word the
            // operator asked for. Green once a vehicle is connected and talking. Clicking it
            // is the connect button: without a vehicle it opens QGC's link chooser, the page
            // the status label on the left also opens; with one, the links now up, each with
            // its own disconnect.
            Item {
                id:                    linkIndicator
                Layout.fillHeight:     true
                Layout.preferredWidth: linkRow.implicitWidth

                readonly property color tint: root._linkUp ? "#42d66b" : "#ff9c46"

                Row {
                    id:                     linkRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing:                6

                    QGCColoredImage {
                        anchors.verticalCenter: parent.verticalCenter
                        width:                  root._menuIconSize * 1.15
                        height:                 width
                        source:                 root._linkUp ? "/res/police_plug_on.svg" : "/res/police_plug_off.svg"
                        color:                  linkIndicator.tint
                        fillMode:               Image.PreserveAspectFit
                        sourceSize.height:      height
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color:                  linkIndicator.tint
                        font.bold:              true
                        font.pixelSize:         Math.max(12, ScreenTools.defaultFontPixelHeight * 0.75)
                        text:                   root._linkUp ? qsTr("Connect") : qsTr("Disconnect")
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked:    mainWindow.showIndicatorDrawer(root._activeVehicle ? linkConnectedPage : linkSelectPage,
                                                                 linkIndicator)
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

        property alias title: gripTitle.text
        property alias slot: contentSlot

        width:  root._windowWidth
        height: root._gripHeight + (root._windowWidth * 9 / 16)
        z:      10

        Rectangle {
            anchors.fill: parent
            radius:       6
            color:        root._panelColor
            border.color: "#526675"
            border.width: 1
        }

        Rectangle {
            id:              gripBar
            anchors.left:    parent.left
            anchors.right:   parent.right
            anchors.top:     parent.top
            anchors.margins: 1
            height:          root._gripHeight
            radius:          5
            color:           "#5a3a4a5c"

            Text {
                id:               gripTitle
                anchors.centerIn: parent
                color:            "white"
                font.bold:        true
                font.pixelSize:   Math.max(11, ScreenTools.defaultFontPixelHeight * 0.65)
                elide:            Text.ElideRight
                width:            parent.width - 16
                horizontalAlignment: Text.AlignHCenter
            }

            // No xAxis/yAxis limits here: their bindings re-evaluate as the dashboard
            // resizes and yank an idle window to the range edge. Clamp on release instead.
            DragHandler {
                target: win
                onActiveChanged: {
                    if (active) {
                        root._userMovedWindows = true
                    } else {
                        win.x = Math.max(4, Math.min(win.x, root.width - win.width - 4))
                        win.y = Math.max(topBar.height + 4, Math.min(win.y, root.height - root._bottomInset - win.height - 4))
                    }
                }
            }
        }

        Item {
            id:              contentSlot
            anchors.left:    parent.left
            anchors.right:   parent.right
            anchors.top:     gripBar.bottom
            anchors.bottom:  parent.bottom
            anchors.margins: 2
        }
    }

    CameraWindow {
        id:    primaryWindow
        title: qsTr("CAM")
    }

    CameraWindow {
        id:    secondaryWindow
        title: qsTr("EO")
    }

    CameraWindow {
        id:    sharedWindow
        title: qsTr("IR")
    }

    // Forward-looking FPV camera on the air unit's second LAN port. Only appears once an RTSP
    // address is configured, so an airframe flown without one keeps the three-window layout.
    CameraWindow {
        id:      fpvWindow
        title:   qsTr("FPV")
        visible: root._fpvConfigured
    }

    // ------------------------------------------------------------------- fly tools
    //
    // Takeoff, land, return, pause, gripper and the pre-flight checklist. QGC keeps these down
    // the left edge; the police layout dropped them with FlyViewWidgetLayer and left nothing
    // there. The guided actions resolve _guidedController off the QML context chain, which the
    // widget layer would have supplied.
    readonly property var _guidedController: guidedController

    // Text-only buttons in Korean: the stock strip's pictograms are QGC's own and read as
    // such. AI folds the camera, AI, speaker and return controls into this column as a
    // drop panel, so the aircraft and its payload are driven from the one place.
    ToolStripActionList {
        id: policeToolActions

        model: [
            PreFlightCheckListShowAction {
                text:        qsTr("점검표")
                iconSource:  ""
                onTriggered: root._showPreFlightChecklist()
            },
            GuidedActionTakeoff            { text: qsTr("이륙");     iconSource: "" },
            GuidedActionLand               { text: qsTr("착륙");     iconSource: "" },
            GuidedActionRTL                { text: qsTr("복귀");     iconSource: "" },
            GuidedActionPause              { text: qsTr("일시정지"); iconSource: "" },
            FlyViewAdditionalActionsButton { text: qsTr("동작");     iconSource: "" },
            FlyViewGripperButton           { text: qsTr("그리퍼");   iconSource: "" },
            SiyiCameraToolStripAction      { text: qsTr("카메라");   iconSource: "" },
            ToolStripAction {
                text:               qsTr("AI")
                iconSource:         ""
                dropPanelComponent: controlPanelComponent
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
            Layout.alignment:       Qt.AlignBottom
            settingsGroup:          factValueGrid.telemetryBarSettingsGroup
            specificVehicleForCard: null // Tracks the active vehicle
        }
    }

    // ------------------------------------------------------------------ control panel
    //
    // Opened from the AI button in the fly tool strip and drawn beside it by the strip's
    // own drop panel, which also closes it on a press anywhere else. Status lines first,
    // then the buttons; the whole column is sized to its content.
    Component {
        id: controlPanelComponent

        ColumnLayout {
            id:      controlPanel
            width:   Math.max(ScreenTools.minTouchPixels * 4, ScreenTools.defaultFontPixelWidth * 17)
            spacing: 4

            Text {
                color:          root._accentColor
                font.bold:      true
                font.pixelSize: Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                text:           qsTr("AI 상태")
            }

            Text {
                color:          App.SiyiAiController.connected
                                    ? (App.SiyiAiController.recognitionEnabled ? "#42d66b" : "white")
                                    : "#ff9c46"
                font.pixelSize: Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                text: {
                    if (!QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue) {
                        return qsTr("꺼짐")
                    }
                    if (!App.SiyiAiController.connected) {
                        return qsTr("모듈 연결 대기")
                    }
                    if (!App.SiyiAiController.recognitionEnabled) {
                        return qsTr("인식 꺼짐")
                    }
                    return App.SiyiAiController.hasTarget
                        ? (App.SiyiAiController.targetLost
                            ? qsTr("표적 유실: %1").arg(App.SiyiAiController.targetTypeName)
                            : qsTr("추적 중: %1").arg(App.SiyiAiController.targetTypeName))
                        : qsTr("인식 중 · 길게 눌러 표적 지정")
                }
            }

            Text {
                visible:        root.aiTargetInfo.length > 0
                color:          "#ffd166"
                font.pixelSize: Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                wrapMode:       Text.WordWrap
                Layout.fillWidth: true
                text:           qsTr("표적 %1").arg(root.aiTargetInfo)
            }

            Text {
                color:          App.SiyiCameraController.connected ? "#42d66b" : "#ff9c46"
                font.pixelSize: Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                text:           App.SiyiCameraController.connected
                                ? qsTr("ZT30 연결 · 줌 %1x").arg(Number(App.SiyiCameraController.zoomMultiple).toFixed(1))
                                : qsTr("ZT30 연결 대기")
            }

            Text {
                visible:        QGroundControl.settingsManager.speakerSettings.enabled.rawValue
                color:          App.SpeakerController.playing ? "#ffcc33"
                                    : (App.SpeakerController.connected ? "#42d66b" : "#ff9c46")
                font.pixelSize: Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                text: {
                    if (!App.SpeakerController.connected) {
                        return qsTr("스피커 대기")
                    }
                    return App.SpeakerController.playing
                        ? qsTr("방송 중 %1").arg(App.SpeakerController.currentTrack)
                        : qsTr("스피커 준비")
                }
            }

            Text {
                color:          "white"
                font.pixelSize: Math.max(12, ScreenTools.defaultFontPixelHeight * 0.72)
                text:           App.SiyiCameraController.rangefinderAvailable
                                ? qsTr("LRF %1 m").arg(Number(App.SiyiCameraController.rangefinderDistance).toFixed(1))
                                : qsTr("LRF --")
            }

            Item { Layout.preferredHeight: 2 }

            Repeater {
                model: [
                    { label: qsTr("3분할"), action: "triple" },
                    { label: qsTr("줌"), action: "zoomOnly" },
                    { label: qsTr("20x"), action: "zoom" },
                    { label: qsTr("중앙"), action: "center" },
                    { label: qsTr("촬영"), action: "photo" }
                ]

                delegate: Button {
                    required property var modelData
                    Layout.fillWidth:       true
                    Layout.preferredHeight: root._touchHeight
                    text:                   modelData.label
                    enabled:                App.SiyiCameraController.connected
                    onClicked: {
                        if (modelData.action === "triple") {
                            root._applyTripleView()
                        } else if (modelData.action === "zoomOnly") {
                            // Image mode 3: main = zoom at full width, sub = thermal.
                            App.SiyiCameraController.setCameraImageType(3)
                            root.splitMainStream = false
                        } else if (modelData.action === "zoom") {
                            App.SiyiCameraController.setZoom(20)
                        } else if (modelData.action === "center") {
                            App.SiyiCameraController.center()
                        } else if (modelData.action === "photo") {
                            App.SiyiCameraController.takePhoto()
                        }
                    }
                }
            }

            Button {
                Layout.fillWidth:       true
                Layout.preferredHeight: root._touchHeight
                enabled:                App.SiyiAiController.connected
                text:                   App.SiyiAiController.hasTarget
                                            ? qsTr("추적 해제")
                                            : (App.SiyiAiController.recognitionEnabled ? qsTr("AI 끄기") : qsTr("AI 켜기"))
                onClicked: {
                    if (App.SiyiAiController.hasTarget) {
                        App.SiyiAiController.cancelTracking()
                    } else {
                        App.SiyiAiController.setRecognition(!App.SiyiAiController.recognitionEnabled)
                    }
                }
            }

            Button {
                id:                     broadcastButton
                Layout.fillWidth:       true
                Layout.preferredHeight: root._touchHeight
                visible:                QGroundControl.settingsManager.speakerSettings.enabled.rawValue
                enabled:                App.SpeakerController.connected
                text:                   App.SpeakerController.playing ? qsTr("방송 정지") : qsTr("경고방송")
                onClicked: {
                    if (App.SpeakerController.playing) {
                        App.SpeakerController.stopPlayback()
                    } else {
                        broadcastPopup.open()
                    }
                }

                Popup {
                    id:      broadcastPopup
                    y:       -height - 6
                    x:       (broadcastButton.width - width) / 2
                    padding: 6
                    modal:   true
                    dim:     false

                    background: Rectangle {
                        color:        "#f2121b24"
                        border.color: "#526675"
                        border.width: 1
                        radius:       5
                    }

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
                                Layout.preferredWidth:  Math.max(ScreenTools.minTouchPixels * 4, ScreenTools.defaultFontPixelWidth * 14)
                                Layout.preferredHeight: root._touchHeight
                                // Track numbers are 1 based on the payload.
                                text:                   qsTr("%1. %2").arg(index + 1).arg(modelData)
                                onClicked: {
                                    broadcastPopup.close()
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
                                                 App.SpeakerController.trackCount !== App.SpeakerController.messageNames.length
                            text:                qsTr("페이로드 파일 %1개 · 이름 %2개 — 설정에서 맞춰주세요")
                                                     .arg(App.SpeakerController.trackCount)
                                                     .arg(App.SpeakerController.messageNames.length)
                        }
                    }
                }
            }

            Button {
                id:                     rtlButton
                Layout.fillWidth:       true
                Layout.preferredHeight: root._touchHeight
                text:                   qsTr("복귀 고도")
                // Ternary rather than &&: the controller is null before the guided layer is
                // built, and `null && x` yields undefined, which will not assign to a bool.
                enabled:                root.guidedController ? root.guidedController.showRTL : false
                onClicked:              rtlAltPopup.open()

                Popup {
                    id:      rtlAltPopup
                    y:       -height - 6
                    x:       (rtlButton.width - width) / 2
                    padding: 6
                    modal:   true
                    dim:     false

                    background: Rectangle {
                        color:        "#f2121b24"
                        border.color: "#526675"
                        border.width: 1
                        radius:       5
                    }

                    onOpened: customAltField.text = ""

                    ColumnLayout {
                        spacing: 6

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            color:            "white"
                            font.bold:        true
                            font.pixelSize:   Math.max(12, ScreenTools.defaultFontPixelHeight * 0.7)
                            text:             qsTr("복귀 고도")
                        }

                        RowLayout {
                            spacing: 5

                            Repeater {
                                model: [10, 50, 100]

                                delegate: Button {
                                    required property int modelData
                                    Layout.preferredWidth:  Math.max(ScreenTools.minTouchPixels * 1.4, ScreenTools.defaultFontPixelWidth * 6)
                                    Layout.preferredHeight: root._touchHeight
                                    text:                   qsTr("%1 m").arg(modelData)
                                    onClicked: {
                                        rtlAltPopup.close()
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
                                    rtlAltPopup.close()
                                    root._returnAt(alt)
                                }
                            }
                        }
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
        }

        // The map, at the size and place of the top camera window, so the aircraft's position
        // stays in view while a camera fills the screen. A tap swaps back: map full, camera in
        // its window. Mirrored from the live map item rather than a second map instance, the
        // way the EO window mirrors the main video; the texture is kept at the window's own
        // size so the copy costs little.
        Item {
            id:      mapPip
            width:   root._windowWidth
            height:  root._gripHeight + (root._windowWidth * 9 / 16)
            x:       parent.width - width - 8
            y:       8
            z:       3
            visible: root.mapItem !== null

            Rectangle {
                anchors.fill: parent
                radius:       6
                color:        root._panelColor
                border.color: "#526675"
                border.width: 1
            }

            Rectangle {
                id:              mapPipGrip
                anchors.left:    parent.left
                anchors.right:   parent.right
                anchors.top:     parent.top
                anchors.margins: 1
                height:          root._gripHeight
                radius:          5
                color:           "#5a3a4a5c"

                Text {
                    anchors.centerIn: parent
                    color:            "white"
                    font.bold:        true
                    font.pixelSize:   Math.max(11, ScreenTools.defaultFontPixelHeight * 0.65)
                    text:             qsTr("지도 · 터치: 전환")
                }
            }

            ShaderEffectSource {
                id:              mapMirror
                anchors.left:    parent.left
                anchors.right:   parent.right
                anchors.top:     mapPipGrip.bottom
                anchors.bottom:  parent.bottom
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
                onTapped: root._toggleExpanded(root.expandedPanel)
            }
        }

        Rectangle {
            anchors.right:   parent.right
            anchors.bottom:  parent.bottom
            anchors.margins: 10
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

    PoliceDroneCameraPanel {
        id:                   primaryPanel
        parent:               root.expandedPanel === "primary" ? fullscreenLayer : primaryWindow.slot
        anchors.fill:         parent
        panelTitle:           qsTr("CAM · 줌")
        panelDetail:          qsTr("드래그: 짐벌 · 터치: 전체화면")
        streamObjectName:     "videoContent"
        // Image mode 2 packs zoom|wide side by side on the main stream. This surface holds
        // the whole frame; the EO panel mirrors its right half.
        videoCropHalf:        root.splitMainStream ? -1 : 0
        aiTargetVisible:      root.aiTargetVisible
        aiTargetX:            root.aiTargetX
        aiTargetY:            root.aiTargetY
        aiTargetWidth:        root.aiTargetWidth
        aiTargetHeight:       root.aiTargetHeight
        aiTargetLabel:        root.aiTargetLabel
        aiTargetInfo:         root.aiTargetInfo
        targetPickEnabled:    root._aiPickEnabled
        onActivated:          root._toggleExpanded("primary")
        onTargetPicked:       (nx, ny) => App.SiyiAiController.trackPoint(nx, ny)
    }

    PoliceDroneCameraPanel {
        id:                   secondaryPanel
        parent:               root.expandedPanel === "secondary" ? fullscreenLayer : secondaryWindow.slot
        anchors.fill:         parent
        panelTitle:           qsTr("EO · 광각")
        panelDetail:          root.splitMainStream
                                  ? qsTr("분할 스트림 우측")
                                  : (root._aiNeedsFullZoomMain ? qsTr("AI 추적 중 사용 불가") : qsTr("CAM 미러"))
        mirrorSource:         primaryPanel.videoSurface
        mirrorHalf:           root.splitMainStream ? 1 : 0
        gimbalControlEnabled: true
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
    }

    PoliceDroneCameraPanel {
        id:                   sharedPipPanel
        parent:               root.expandedPanel === "shared" ? fullscreenLayer : sharedWindow.slot
        anchors.fill:         parent
        panelTitle:           root._aiStreamActive ? qsTr("AI · 인식") : qsTr("IR · 열상")
        panelDetail:          root._aiStreamActive ? qsTr("AI 모듈 스트림") : qsTr("ZT30 보조 스트림")
        streamObjectName:     "thermalVideo"
        targetPickEnabled:    root._aiPickEnabled
        onActivated:          root._toggleExpanded("shared")
        onTargetPicked:       (nx, ny) => App.SiyiAiController.trackPoint(nx, ny)
    }

    PoliceDroneCameraPanel {
        id:               fpvPanel
        parent:           root.expandedPanel === "fpv" ? fullscreenLayer : fpvWindow.slot
        anchors.fill:     parent
        panelTitle:       qsTr("FPV · 전방")
        panelDetail:      qsTr("에어유닛 LAN2")
        streamObjectName: "fpvVideo"
        // No gimbal drag and no AI picking: this is a fixed forward camera, not the pod.
        onActivated:      root._toggleExpanded("fpv")
    }
}
