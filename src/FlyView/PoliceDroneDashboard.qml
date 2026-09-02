import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.FlyView
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
        anchors.bottom:           aiStatusBar.top
        anchors.bottomMargin:     12
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

    readonly property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property var _battery:       _activeVehicle && _activeVehicle.batteries.count > 0 ? _activeVehicle.batteries.get(0) : null
    readonly property real _touchHeight:  Math.max(48, ScreenTools.defaultFontPixelHeight * 2.8)
    // The top bar hosts QGC's own toolbar indicators, which are laid out against
    // ScreenTools.toolbarHeight; anything shorter clips them.
    readonly property real _statusHeight: Math.max(42, ScreenTools.toolbarHeight)
    readonly property color _panelColor:  "#e5121b24"
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

    readonly property real _windowWidth: Math.max(200, Math.min(width * 0.24, 360))
    property bool _userMovedWindows: false

    function _dockWindows() {
        const gap = 8
        const yDock = aiStatusBar.y - primaryWindow.height - gap
        const windows = [primaryWindow, secondaryWindow, sharedWindow]
        for (let i = 0; i < windows.length; ++i) {
            windows[i].x = gap + i * (_windowWidth + gap)
            windows[i].y = yDock
        }
    }

    // Re-dock until the operator moves a window; afterwards positions are theirs. Both
    // triggers matter: the bar settles vertically after load, and the window width used for
    // the x spacing follows the root width.
    function _redockIfPristine() {
        if (!_userMovedWindows && aiStatusBar.y > 100) {
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
        target: aiStatusBar
        function onYChanged() { root._redockIfPristine() }
    }

    function _factText(fact, fallback) {
        return fact ? fact.valueString + (fact.units.length > 0 ? " " + fact.units : "") : fallback
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
            anchors.leftMargin:  8
            anchors.rightMargin: 12
            spacing:             10

            // A plain Button paints the style's own opaque background, which read as a white
            // slab on this dark bar. Transparent background plus an explicitly light icon
            // matches how the toolbars in the other views render theirs.
            Button {
                Layout.preferredWidth:  root._touchHeight
                Layout.fillHeight:      true
                onClicked:              root.menuRequested()

                background: Rectangle {
                    color: parent.down ? "#33ffffff" : "transparent"
                }

                contentItem: QGCColoredImage {
                    source:            "qrc:/qmlimages/Hamburger.svg"
                    color:             "white"
                    fillMode:          Image.PreserveAspectFit
                    sourceSize.height: ScreenTools.defaultFontPixelHeight * 1.4
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
            MainStatusIndicator {
                objectName:        "toolbar_mainStatusIndicator"
                Layout.fillHeight: true
            }

            FlightModeIndicator {
                objectName:        "toolbar_flightModeIndicator"
                Layout.fillHeight: true
                visible:           root._activeVehicle
            }

            Item { Layout.fillWidth: true }

            FlyViewToolBarIndicators {
                Layout.fillHeight:     true
                Layout.preferredWidth: implicitWidth
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
        height: gripBar.height + (root._windowWidth * 9 / 16)
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
            height:          Math.max(24, ScreenTools.defaultFontPixelHeight * 1.3)
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
                        win.y = Math.max(topBar.height + 4, Math.min(win.y, aiStatusBar.y - win.height - 4))
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
        title: qsTr("MAIN")
    }

    CameraWindow {
        id:    secondaryWindow
        title: qsTr("EO")
    }

    CameraWindow {
        id:    sharedWindow
        title: qsTr("IR")
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

    FlyViewBottomRightRowLayout {
        id:                   flightInstruments
        anchors.right:        parent.right
        anchors.rightMargin:  8
        anchors.bottom:       aiStatusBar.top
        anchors.bottomMargin: 8
        // Below the camera windows (z 10) so a window dragged this way passes over the
        // instruments instead of disappearing behind them.
        z:                    3
    }

    Rectangle {
        id: aiStatusBar
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: controlBar.top
        height:         Math.max(34, ScreenTools.defaultFontPixelHeight * 1.7)
        color:          "#eb101820"
        z:              4

        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill:        parent
            anchors.leftMargin:  14
            anchors.rightMargin: 14
            spacing:             18

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

            Item { Layout.fillWidth: true }

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
        }
    }

    Rectangle {
        id: controlBar
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: parent.bottom
        height:         root._touchHeight + 10
        color:          root._panelColor
        z:              4

        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill:        parent
            anchors.margins:     5
            spacing:             6

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
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                    text:              modelData.label
                    enabled:           App.SiyiCameraController.connected
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
                Layout.fillWidth:  true
                Layout.fillHeight: true
                enabled:           App.SiyiAiController.connected
                text:              App.SiyiAiController.hasTarget
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
                id:                broadcastButton
                Layout.fillWidth:  true
                Layout.fillHeight: true
                visible:           QGroundControl.settingsManager.speakerSettings.enabled.rawValue
                enabled:           App.SpeakerController.connected
                text:              App.SpeakerController.playing ? qsTr("방송 정지") : qsTr("경고방송")
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
                                Layout.preferredWidth:  Math.max(150, ScreenTools.defaultFontPixelWidth * 18)
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
                id:                rtlButton
                Layout.fillWidth:  true
                Layout.fillHeight: true
                text:              qsTr("RTL")
                enabled:           root.guidedController && root.guidedController.showRTL
                onClicked:         rtlAltPopup.open()

                Popup {
                    id:     rtlAltPopup
                    y:      -height - 6
                    x:      (rtlButton.width - width) / 2
                    padding: 6
                    modal:  true
                    dim:    false

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
                                    Layout.preferredWidth:  Math.max(64, ScreenTools.defaultFontPixelWidth * 8)
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
                                id:                    customAltField
                                Layout.preferredWidth: Math.max(74, ScreenTools.defaultFontPixelWidth * 9)
                                Layout.preferredHeight: root._touchHeight
                                placeholderText:       qsTr("사용자 설정")
                                inputMethodHints:      Qt.ImhFormattedNumbersOnly
                                validator:             DoubleValidator { bottom: 1; top: 1000; decimals: 0 }
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

        Rectangle {
            anchors.right:   parent.right
            anchors.top:     parent.top
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
        panelTitle:           qsTr("MAIN · 줌")
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
                                  : (root._aiNeedsFullZoomMain ? qsTr("AI 추적 중 사용 불가") : qsTr("MAIN 미러"))
        mirrorSource:         primaryPanel.videoSurface
        mirrorHalf:           root.splitMainStream ? 1 : 0
        gimbalControlEnabled: true
        aiTargetVisible:      root.aiTargetVisible
        aiTargetX:            root.aiTargetX
        aiTargetY:            root.aiTargetY
        aiTargetWidth:        root.aiTargetWidth
        aiTargetHeight:       root.aiTargetHeight
        aiTargetLabel:        root.aiTargetLabel
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
}
