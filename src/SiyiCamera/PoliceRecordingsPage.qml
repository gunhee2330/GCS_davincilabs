import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// 영상 기록: the pod's recorded videos and their copies on this tablet, opened from the menu.
///
/// The procurement spec wants video records easy to check (p6 차) and managed: stored, deleted on
/// schedule, limited by who is using the tablet (p5), and named after the recording's start
/// (p9). The pod keeps its videos on its own SD card; this lists them, brings a copy to the tablet
/// under the RFP name, and plays it. Shown in the tool drawer under the settings screens' bar,
/// drawn in the settings look after the police mockup, where 1 px of the 1920 wide mockup is
/// _px(1).
Rectangle {
    id:         page
    objectName: "policeRecordingsPage"
    color:      qgcPal.settingsPanel

    /// The settings screens' controls under here: their boxes, text size and touch targets
    readonly property bool settingsMockupLook: true

    readonly property var  _recordings: App.SiyiRecordings
    readonly property var  _files:      _recordings.files
    readonly property var  _download:   _recordings.download
    readonly property var  _settings:   QGroundControl.settingsManager.siyiCameraSettings
    readonly property bool _admin:      QGroundControl.corePlugin.showAdvancedUI
    /// 접근 사용자 관리자: nothing here without the administrator
    readonly property bool _locked:     _settings.recordingAdminOnly.rawValue && !_admin
    readonly property var  _deleteDays: [ 7, 30, 90, 0 ]

    /// The row the right pane describes, by key
    property string _currentKey: ""
    readonly property var _selected: {
        const index = _files.findIndex(file => file.key === _currentKey)
        return index >= 0 ? _files[index] : (_files.length > 0 ? _files[0] : null)
    }
    readonly property int _podCount:      _files.filter(file => file.onPod).length
    readonly property int _receivedCount: _files.filter(file => file.received).length
    property string _message: ""
    property bool   _messageIsError: false

    /// Same placeholder PIN and lockout as the 개발자 settings page (DeveloperModeSettings._developerPin):
    /// the administrator here is the developer mode that PIN turns on.
    // ponytail: duplicated literal; move both to the Android Keystore together, as that page's TODO says
    readonly property string _adminPin:       "704183"
    readonly property int    _maxPinFailures: 5
    property int    _pinFailures: 0
    property var    _pendingAdminAction: null

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    function _px(mockupPixels) {
        return ScreenTools.mockupUnit * mockupPixels / 19.2
    }

    /// Runs \a action as the administrator, asking for the PIN first when developer mode is off
    function _asAdmin(action) {
        if (_admin) {
            action()
            return
        }
        _pendingAdminAction = action
        pinDialogFactory.open()
    }

    function _twoDigits(n) {
        return (n < 10 ? "0" : "") + n
    }

    function _dateTimeText(ms) {
        return ms > 0 ? Qt.formatDateTime(new Date(ms), "yyyy-MM-dd hh:mm:ss") : "—"
    }

    function _durationText(seconds) {
        if (seconds === undefined || seconds < 0) {
            return ""
        }
        const h = Math.floor(seconds / 3600)
        const m = Math.floor((seconds % 3600) / 60)
        const s = Math.floor(seconds % 60)
        return (h > 0 ? h + ":" + _twoDigits(m) : _twoDigits(m)) + ":" + _twoDigits(s)
    }

    function _sizeText(bytes) {
        if (bytes === undefined || bytes < 0) {
            return ""
        }
        if (bytes >= 1024 * 1024 * 1024) {
            return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB"
        }
        if (bytes >= 1024 * 1024) {
            return Math.round(bytes / (1024 * 1024)) + " MB"
        }
        return Math.max(1, Math.round(bytes / 1024)) + " KB"
    }

    /// When the file was made and where that time came from, as the list says it
    function _timeText(file) {
        switch (file.timeSource) {
        case "log":
            return qsTr("녹화 시작 %1").arg(_dateTimeText(file.startMs))
        case "pod":
            return qsTr("카메라 파일 시각 %1 (녹화 기록 없음)").arg(_dateTimeText(file.startMs))
        case "download":
            return qsTr("받은 시각 %1 (카메라 파일 시각 없음)").arg(_dateTimeText(file.startMs))
        default:
            return qsTr("녹화 시각 모름")
        }
    }

    function _positionText(file) {
        return file.hasPosition ? file.lat.toFixed(4) + ", " + file.lon.toFixed(4) : qsTr("없음")
    }

    function _deleteText(file) {
        if (!file.received) {
            return "—"
        }
        if (!file.deleteAtMs) {
            return qsTr("자동 삭제 꺼짐")
        }
        const days = Math.max(0, Math.ceil((file.deleteAtMs - Date.now()) / 86400000))
        return qsTr("%1 (%2일 뒤)").arg(Qt.formatDate(new Date(file.deleteAtMs), "yyyy-MM-dd")).arg(days)
    }

    function _select(key) {
        player.stop()
        player.source = ""
        _currentKey = key
    }

    function _play() {
        if (player.playbackState === MediaPlayer.PlayingState) {
            player.pause()
            return
        }
        const url = _recordings.playbackUrl(_selected.key)
        if (player.source.toString() !== url.toString()) {
            player.source = url
        }
        player.play()
    }

    function _export() {
        const path = _recordings.exportCopy(_selected.key)
        _messageIsError = path === ""
        _message = _messageIsError ? qsTr("내보내지 못했습니다") : qsTr("내보냈습니다: %1").arg(path)
    }

    function _remove() {
        const key = _selected.key
        const name = _selected.localName
        QGroundControl.showMessageDialog(page, qsTr("삭제"), qsTr("태블릿에서 %1을 지울까요? 카메라의 파일은 남습니다.").arg(name),
                                         Dialog.Yes | Dialog.No, function() {
                                             player.stop()
                                             player.source = ""
                                             _messageIsError = !_recordings.remove(key)
                                             _message = _messageIsError ? qsTr("지우지 못했습니다") : qsTr("지웠습니다: %1").arg(name)
                                         })
    }

    Component.onCompleted: _recordings.refresh()

    Connections {
        target: App.SiyiCameraController
        function onConnectedChanged() {
            if (App.SiyiCameraController.connected) {
                page._recordings.refresh()
            }
        }
    }

    // This needs to block click event leakage to the view underneath.
    DeadMouseArea {
        anchors.fill: parent
    }

    QGCPopupDialogFactory {
        id:                 pinDialogFactory
        dialogComponent:    pinDialogComponent
    }

    Component {
        id: pinDialogComponent

        QGCPopupDialog {
            objectName: "recordingsPinDialog"
            title:      qsTr("관리자 확인")
            buttons:    Dialog.Ok | Dialog.Cancel

            property string _pinMessage: ""

            onAccepted: {
                if (lockoutTimer.running) {
                    preventClose = true
                    return
                }
                if (pinField.text === page._adminPin) {
                    page._pinFailures = 0
                    QGroundControl.corePlugin.showAdvancedUI = true
                    const action = page._pendingAdminAction
                    page._pendingAdminAction = null
                    if (action) {
                        action()
                    }
                    return
                }
                pinField.text = ""
                preventClose = true
                page._pinFailures++
                if (page._pinFailures >= page._maxPinFailures) {
                    page._pinFailures = 0
                    lockoutTimer.restart()
                    _pinMessage = qsTr("잘못 입력한 횟수가 많아 60초 동안 잠깁니다.")
                } else {
                    _pinMessage = qsTr("PIN이 맞지 않습니다.")
                }
            }
            onRejected: page._pendingAdminAction = null

            ColumnLayout {
                spacing: ScreenTools.defaultFontPixelHeight / 2

                QGCLabel {
                    text: qsTr("관리자 PIN을 입력하세요. 개발자 모드가 켜집니다.")
                }

                QGCTextField {
                    id:                     pinField
                    objectName:             "recordingsPinField"
                    Layout.fillWidth:       true
                    echoMode:               TextInput.Password
                    maximumLength:          6
                    numericValuesOnly:      true
                    enabled:                !lockoutTimer.running
                    Component.onCompleted:  forceActiveFocus()
                }

                QGCLabel {
                    text:       _pinMessage
                    visible:    text !== ""
                    color:      qgcPal.colorOrange
                }
            }
        }
    }

    Timer {
        id:         lockoutTimer
        interval:   60000
    }

    MediaPlayer {
        id:             player
        objectName:     "recordingsMediaPlayer"
        videoOutput:    videoOutput
    }

    // Inline components do not see the ids of this file, so each carries its own palette and
    // works the mockup's pixels out itself.

    /// The settings cards' quiet caption
    component Caption: QGCLabel {
        font.pointSize:     ScreenTools.defaultFontPointSize * 0.85
        font.letterSpacing: ScreenTools.mockupUnit * 0.019
        color:              _captionPal.secondaryText
        QGCPalette { id: _captionPal; colorGroupEnabled: true }
    }

    /// The settings card: its fill, hairline edge and corner
    component Card: Rectangle {
        color:               _cardPal.card
        border.color:        _cardPal.cardBorder
        border.width:        ScreenTools.hairline
        border.pixelAligned: false
        radius:              ScreenTools.mockupUnit * 10 / 19.2
        QGCPalette { id: _cardPal; colorGroupEnabled: true }
    }

    /// The upright divider between the header card's three settings
    component Divider: Rectangle {
        Layout.fillHeight:      true
        Layout.topMargin:       ScreenTools.mockupUnit * 18 / 19.2
        Layout.bottomMargin:    ScreenTools.mockupUnit * 18 / 19.2
        Layout.leftMargin:      ScreenTools.mockupUnit * 30 / 19.2
        Layout.rightMargin:     ScreenTools.mockupUnit * 30 / 19.2
        implicitWidth:          1
        color:                  _dividerPal.cardBorder
        QGCPalette { id: _dividerPal; colorGroupEnabled: true }
    }

    /// A video's placeholder picture: a dark frame with a play mark
    component Thumbnail: Rectangle {
        color:  "#0b1118"
        radius: ScreenTools.mockupUnit * 6 / 19.2

        property real markSize: ScreenTools.mockupUnit * 70 / 19.2

        Rectangle {
            anchors.centerIn:   parent
            width:              parent.markSize
            height:             width
            radius:             width / 2
            color:              "transparent"
            border.color:       _thumbPal.secondaryText
            border.width:       Math.max(1, width / 23)
        }

        QGCColoredImage {
            anchors.centerIn:           parent
            anchors.horizontalCenterOffset: parent.markSize * 0.06
            width:                      parent.markSize * 0.45
            height:                     width
            source:                     "/InstrumentValueIcons/play.svg"
            color:                      _thumbPal.secondaryText
        }

        QGCPalette { id: _thumbPal; colorGroupEnabled: true }
    }

    ColumnLayout {
        anchors.fill:           parent
        anchors.leftMargin:     page._px(40)
        anchors.rightMargin:    page._px(40)
        anchors.topMargin:      page._px(25)
        anchors.bottomMargin:   page._px(20)
        spacing:                page._px(24)

        // 암호화 저장, 자동 삭제, 접근 사용자
        Card {
            objectName:             "recordingsHeader"
            Layout.fillWidth:       true
            Layout.preferredHeight: page._px(96)

            RowLayout {
                anchors.fill:           parent
                anchors.leftMargin:     page._px(30)
                anchors.rightMargin:    page._px(30)
                spacing:                0

                // Wider than the mockup's third, as its note is longer than the mockup's 켜짐
                RowLayout {
                    Layout.fillWidth:       true
                    Layout.preferredWidth:  1.4
                    spacing:                page._px(24)

                    QGCLabel {
                        text:               qsTr("암호화 저장")
                        font.weight:        Font.DemiBold
                        font.pointSize:     ScreenTools.defaultFontPointSize * 1.04
                    }

                    // No AES this build can reach on both the tablet and the desktop yet
                    QGCCheckBoxSlider {
                        objectName: "recordingsEncrypt"
                        checked:    false
                        enabled:    false
                    }

                    QGCLabel {
                        objectName:     "recordingsEncryptNote"
                        Layout.fillWidth: true
                        text:           qsTr("암호화 모듈 준비 중")
                        color:          qgcPal.secondaryText
                        font.pointSize: ScreenTools.defaultFontPointSize * 0.9
                        elide:          Text.ElideRight
                    }
                }

                Divider { }

                RowLayout {
                    Layout.fillWidth:       true
                    Layout.preferredWidth:  0.8
                    spacing:                page._px(24)

                    QGCLabel {
                        text:               qsTr("자동 삭제")
                        font.weight:        Font.DemiBold
                        font.pointSize:     ScreenTools.defaultFontPointSize * 1.04
                    }

                    QGCComboBox {
                        id:                     deleteCombo
                        objectName:             "recordingsAutoDelete"
                        Layout.preferredWidth:  page._px(170)
                        model:                  [ qsTr("7일"), qsTr("30일"), qsTr("90일"), qsTr("끄기") ]
                        currentIndex:           Math.max(0, page._deleteDays.indexOf(page._settings.recordingAutoDeleteDays.rawValue))
                        onActivated: (index) => {
                            currentIndex = Qt.binding(() => Math.max(0, page._deleteDays.indexOf(page._settings.recordingAutoDeleteDays.rawValue)))
                            page._asAdmin(() => page._settings.recordingAutoDeleteDays.rawValue = page._deleteDays[index])
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                Divider { }

                RowLayout {
                    Layout.fillWidth:       true
                    Layout.preferredWidth:  1.1
                    spacing:                page._px(24)

                    QGCLabel {
                        text:               qsTr("접근 사용자")
                        font.weight:        Font.DemiBold
                        font.pointSize:     ScreenTools.defaultFontPointSize * 1.04
                    }

                    QGCComboBox {
                        objectName:             "recordingsAccess"
                        Layout.preferredWidth:  page._px(300)
                        model:                  [ qsTr("관리자, 운용자"), qsTr("관리자") ]
                        currentIndex:           page._settings.recordingAdminOnly.rawValue ? 1 : 0
                        onActivated: (index) => {
                            currentIndex = Qt.binding(() => page._settings.recordingAdminOnly.rawValue ? 1 : 0)
                            page._asAdmin(() => page._settings.recordingAdminOnly.rawValue = index === 1)
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }

        // With 관리자 only and developer mode off, the records stay behind the PIN
        Card {
            objectName:         "recordingsLocked"
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            visible:            page._locked

            ColumnLayout {
                anchors.centerIn:   parent
                spacing:            page._px(24)

                QGCLabel {
                    Layout.alignment:   Qt.AlignHCenter
                    text:               qsTr("관리자만 볼 수 있습니다")
                }

                QGCButton {
                    objectName:         "recordingsUnlock"
                    Layout.alignment:   Qt.AlignHCenter
                    text:               qsTr("관리자 확인")
                    primary:            true
                    onClicked:          page._asAdmin(() => {})
                }
            }
        }

        RowLayout {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            spacing:            page._px(26)
            visible:            !page._locked

            ColumnLayout {
                Layout.fillWidth:   true
                Layout.fillHeight:  true
                spacing:            0

                // Where the list comes from and the pod's link
                Card {
                    objectName:             "recordingsSource"
                    Layout.fillWidth:       true
                    Layout.preferredHeight: page._px(84)

                    RowLayout {
                        anchors.fill:           parent
                        anchors.leftMargin:     page._px(26)
                        anchors.rightMargin:    page._px(14)
                        spacing:                page._px(22)

                        QGCLabel {
                            text:           qsTr("출처: 카메라 SD 카드")
                            font.bold:      true
                        }

                        Rectangle {
                            Layout.preferredWidth:  page._px(16)
                            Layout.preferredHeight: page._px(16)
                            radius:                 width / 2
                            color:                  App.SiyiCameraController.connected ? qgcPal.colorGreen : qgcPal.colorGrey
                        }

                        QGCLabel {
                            objectName:     "recordingsPodState"
                            text:           App.SiyiCameraController.connected
                                                ? qsTr("%1 연결됨").arg(App.SiyiCameraController.model !== "" ? App.SiyiCameraController.model : qsTr("카메라"))
                                                : qsTr("카메라 연결 안 됨")
                            font.pointSize: ScreenTools.defaultFontPointSize * 0.9
                        }

                        QGCLabel {
                            Layout.fillWidth:   true
                            text:               page._settings.ipAddress.rawValue
                            color:              qgcPal.secondaryText
                            font.pointSize:     ScreenTools.defaultFontPointSize * 0.82
                            elide:              Text.ElideRight
                        }

                        QGCButton {
                            objectName:             "recordingsFetchAll"
                            Layout.preferredHeight: page._px(58)
                            text:                   qsTr("모두 받기")
                            primary:                true
                            enabled:                page._files.some(file => file.onPod && !file.received)
                            onClicked:              page._recordings.fetchAll()
                        }
                    }
                }

                // The counts, and on the right what the last action came to
                RowLayout {
                    Layout.fillWidth:       true
                    Layout.topMargin:       page._px(18)
                    Layout.bottomMargin:    page._px(12)
                    spacing:                page._px(24)

                    Caption {
                        objectName:             "recordingsSummary"
                        Layout.maximumWidth:    page._px(700)
                        text: page._recordings.listing ? qsTr("카메라 파일 목록을 읽는 중")
                                                       : qsTr("카메라에 녹화 파일 %1개, 태블릿에 받은 파일 %2개, 최신순").arg(page._podCount).arg(page._receivedCount)
                                                         + (page._recordings.listError !== "" ? "    " + page._recordings.listError : "")
                        elide: Text.ElideRight
                    }

                    QGCLabel {
                        objectName:             "recordingsMessage"
                        Layout.fillWidth:       true
                        horizontalAlignment:    Text.AlignRight
                        text:                   page._download.error !== "" ? page._download.error : page._message
                        color:                  page._download.error !== "" || page._messageIsError ? qgcPal.colorOrange : qgcPal.text
                        font.pointSize:         ScreenTools.defaultFontPointSize * 0.85
                        elide:                  Text.ElideMiddle
                    }
                }

                Card {
                    Layout.fillWidth:   true
                    Layout.fillHeight:  true
                    clip:               true

                    QGCListView {
                        id:                 list
                        objectName:         "recordingsList"
                        anchors.fill:       parent
                        anchors.margins:    ScreenTools.hairline
                        // A count, not the list: a download changing a file keeps the rows and the
                        // scroll where they are, which a new list for a model would both reset.
                        model:              page._files.length

                        QGCLabel {
                            anchors.centerIn:   parent
                            visible:            list.count === 0 && !page._recordings.listing
                            text:               qsTr("녹화 파일이 없습니다")
                            color:              qgcPal.secondaryText
                        }

                        delegate: Rectangle {
                            id:         row
                            objectName: "recordingsRow_" + index
                            width:      ListView.view.width
                            height:     page._px(126)
                            color:      selected ? qgcPal.selectedRow : "transparent"

                            required property int index
                            readonly property var  file:        page._files[index] || ({})
                            readonly property bool selected:    !!page._selected && page._selected.key === file.key
                            readonly property bool downloading: page._download.key === file.key
                            readonly property bool queued:      page._download.queued.indexOf(file.key) >= 0

                            Rectangle {
                                width:      parent.width
                                height:     1
                                color:      qgcPal.cardBorder
                                opacity:    0.6
                                visible:    row.index > 0
                            }

                            Rectangle {
                                anchors.left:   parent.left
                                anchors.top:    parent.top
                                anchors.bottom: parent.bottom
                                width:          page._px(5)
                                color:          qgcPal.buttonHighlight
                                visible:        row.selected
                            }

                            MouseArea {
                                anchors.fill:   parent
                                onClicked:      page._select(row.file.key)
                            }

                            RowLayout {
                                anchors.fill:           parent
                                anchors.leftMargin:     page._px(22)
                                anchors.rightMargin:    page._px(22)
                                spacing:                page._px(24)

                                Thumbnail {
                                    Layout.preferredWidth:  page._px(176)
                                    Layout.preferredHeight: page._px(99)
                                    markSize:               page._px(40)
                                }

                                ColumnLayout {
                                    Layout.fillWidth:   true
                                    spacing:            page._px(6)

                                    QGCLabel {
                                        Layout.fillWidth:   true
                                        text:               qsTr("카메라 파일 %1").arg(row.file.podName) + (row.file.onPod ? "" : qsTr(" (카메라에 없음)"))
                                        color:              qgcPal.secondaryText
                                        font.pointSize:     ScreenTools.defaultFontPointSize * 0.75
                                        elide:              Text.ElideMiddle
                                    }

                                    RowLayout {
                                        Layout.fillWidth:   true
                                        spacing:            page._px(14)

                                        QGCLabel {
                                            objectName:         "recordingsName_" + row.index
                                            Layout.fillWidth:   row.file.received ? false : true
                                            Layout.maximumWidth: row.width
                                            text:               row.file.received ? row.file.localName
                                                                                       : row.downloading ? qsTr("받는 중")
                                                                                       : row.queued ? qsTr("받기 대기")
                                                                                                    : qsTr("받기 전")
                                            color:              row.file.received ? qgcPal.text : qgcPal.secondaryText
                                            font.pointSize:     ScreenTools.defaultFontPointSize * 0.93
                                            elide:              Text.ElideMiddle
                                        }

                                        Rectangle {
                                            visible:                row.file.received
                                            Layout.preferredWidth:  receivedBadge.implicitWidth + page._px(24)
                                            Layout.preferredHeight: page._px(34)
                                            radius:                 page._px(6)
                                            color:                  Qt.darker(qgcPal.colorGreen, 2.2)

                                            QGCLabel {
                                                id:                 receivedBadge
                                                anchors.centerIn:   parent
                                                text:               qsTr("받음")
                                                color:              "white"
                                                font.bold:          true
                                                font.pointSize:     ScreenTools.defaultFontPointSize * 0.75
                                            }
                                        }

                                        Item { Layout.fillWidth: row.file.received }
                                    }

                                    QGCLabel {
                                        Layout.fillWidth:   true
                                        text:               [ page._timeText(row.file),
                                                              page._durationText(row.file.durationS),
                                                              row.file.received ? page._sizeText(row.file.size) : "" ]
                                                                .filter(part => part !== "").join("    ")
                                        color:              qgcPal.secondaryText
                                        font.pointSize:     ScreenTools.defaultFontPointSize * 0.79
                                        elide:              Text.ElideRight
                                    }
                                }

                                // 받는 중 with how far, 태블릿으로 받기, or nothing once received
                                Item {
                                    Layout.preferredWidth:  page._px(270)
                                    Layout.fillHeight:      true

                                    ColumnLayout {
                                        anchors.left:           parent.left
                                        anchors.right:          parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing:                page._px(10)
                                        visible:                row.downloading

                                        QGCLabel {
                                            objectName:             "recordingsPercent_" + row.index
                                            Layout.alignment:       Qt.AlignRight
                                            text:                   page._download.percent + "%"
                                            font.pointSize:         ScreenTools.defaultFontPointSize * 0.82
                                        }

                                        Rectangle {
                                            Layout.fillWidth:       true
                                            Layout.preferredHeight: page._px(8)
                                            radius:                 height / 2
                                            color:                  Qt.rgba(qgcPal.cardBorder.r, qgcPal.cardBorder.g, qgcPal.cardBorder.b, 0.5)

                                            Rectangle {
                                                width:  parent.width * page._download.percent / 100
                                                height: parent.height
                                                radius: parent.radius
                                                color:  qgcPal.buttonHighlight
                                            }
                                        }
                                    }

                                    QGCButton {
                                        objectName:     "recordingsFetch_" + row.index
                                        anchors.left:   parent.left
                                        anchors.right:  parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        height:         page._px(58)
                                        text:           qsTr("태블릿으로 받기")
                                        pointSize:      ScreenTools.defaultFontPointSize * 0.9
                                        visible:        row.file.onPod && !row.file.received && !row.downloading
                                        enabled:        !row.queued
                                        onClicked:      page._recordings.fetch(row.file.key)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // The chosen video: its player, what it is, and what may be done with it
            Card {
                objectName:             "recordingsDetail"
                Layout.preferredWidth:  page._px(664)
                Layout.fillHeight:      true

                ColumnLayout {
                    anchors.fill:       parent
                    anchors.margins:    page._px(20)
                    spacing:            page._px(12)

                    Rectangle {
                        id:                     playerFrame
                        objectName:             "recordingsPlayer"
                        Layout.fillWidth:       true
                        Layout.preferredHeight: width * 9 / 16
                        color:                  "black"
                        radius:                 page._px(6)

                        VideoOutput {
                            id:             videoOutput
                            anchors.fill:   parent
                        }

                        Thumbnail {
                            anchors.fill:   parent
                            color:          "transparent"
                            visible:        player.playbackState !== MediaPlayer.PlayingState
                        }

                        ColumnLayout {
                            anchors.left:           parent.left
                            anchors.right:          parent.right
                            anchors.bottom:         parent.bottom
                            anchors.leftMargin:     page._px(20)
                            anchors.rightMargin:    page._px(20)
                            anchors.bottomMargin:   page._px(18)
                            spacing:                page._px(8)
                            visible:                !!page._selected && page._selected.received

                            Rectangle {
                                Layout.fillWidth:       true
                                Layout.preferredHeight: page._px(6)
                                radius:                 height / 2
                                color:                  Qt.rgba(qgcPal.cardBorder.r, qgcPal.cardBorder.g, qgcPal.cardBorder.b, 0.8)

                                Rectangle {
                                    width:  player.duration > 0 ? parent.width * player.position / player.duration : 0
                                    height: parent.height
                                    radius: parent.radius
                                    color:  qgcPal.buttonHighlight
                                }
                            }

                            QGCLabel {
                                objectName:     "recordingsPlayerTime"
                                text:           page._durationText(Math.floor(player.position / 1000)) + " / " +
                                                (player.duration > 0 ? page._durationText(Math.floor(player.duration / 1000))
                                                                     : (page._selected && page._selected.durationS >= 0 ? page._durationText(page._selected.durationS) : "--:--"))
                                color:          "white"
                                font.pointSize: ScreenTools.defaultFontPointSize * 0.79
                            }
                        }
                    }

                    QGCLabel {
                        objectName:         "recordingsDetailName"
                        Layout.fillWidth:   true
                        Layout.topMargin:   page._px(6)
                        text:               page._selected ? (page._selected.received ? page._selected.localName : page._selected.podName) : ""
                        wrapMode:           Text.WrapAnywhere
                        font.pointSize:     ScreenTools.defaultFontPointSize * 0.9
                    }

                    GridLayout {
                        Layout.fillWidth:   true
                        columns:            2
                        columnSpacing:      0
                        rowSpacing:         page._px(12)
                        visible:            !!page._selected

                        Repeater {
                            model: page._selected ? [
                                qsTr("촬영 일시"),   page._dateTimeText(page._selected.startMs),
                                qsTr("촬영 위치"),   page._positionText(page._selected),
                                qsTr("길이"),       page._durationText(page._selected.durationS) || "—",
                                qsTr("크기"),       page._sizeText(page._selected.size) || "—",
                                qsTr("카메라 파일"), page._selected.podName,
                                qsTr("보관"),       page._selected.received ? qsTr("태블릿, 암호화 안 됨") : qsTr("카메라"),
                                qsTr("삭제 예정"),   page._deleteText(page._selected) ] : []

                            QGCLabel {
                                required property string modelData
                                required property int    index
                                Layout.preferredWidth:  index % 2 === 0 ? page._px(170) : -1
                                Layout.fillWidth:       index % 2 === 1
                                text:                   modelData
                                font.pointSize:         ScreenTools.defaultFontPointSize * 0.9
                                color:                  index % 2 === 0 ? qgcPal.secondaryText : qgcPal.text
                                elide:                  Text.ElideMiddle
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            page._px(16)

                        QGCButton {
                            objectName:             "recordingsPlay"
                            Layout.fillWidth:       true
                            Layout.preferredWidth:  1
                            Layout.preferredHeight: page._px(64)
                            text:                   player.playbackState === MediaPlayer.PlayingState ? qsTr("일시 정지") : qsTr("재생")
                            primary:                true
                            enabled:                !!page._selected && page._selected.received
                            onClicked:              page._play()
                        }

                        QGCButton {
                            objectName:             "recordingsExport"
                            Layout.fillWidth:       true
                            Layout.preferredWidth:  1
                            Layout.preferredHeight: page._px(64)
                            text:                   qsTr("내보내기")
                            enabled:                !!page._selected && page._selected.received
                            onClicked:              page._asAdmin(page._export)
                        }

                        QGCButton {
                            id:                     deleteButton
                            objectName:             "recordingsDelete"
                            Layout.fillWidth:       true
                            Layout.preferredWidth:  1
                            Layout.preferredHeight: page._px(64)
                            text:                   qsTr("삭제")
                            textColor:              enabled ? qgcPal.colorRed : Qt.rgba(qgcPal.colorRed.r, qgcPal.colorRed.g, qgcPal.colorRed.b, 0.4)
                            enabled:                !!page._selected && page._selected.received
                            onClicked:              page._asAdmin(page._remove)

                            // The mockup's red edge over the button's own
                            Rectangle {
                                anchors.fill:   parent
                                color:          "transparent"
                                radius:         deleteButton.backRadius
                                border.color:   qgcPal.colorRed
                                border.width:   ScreenTools.hairline
                                opacity:        deleteButton.enabled ? 1 : 0.4
                            }
                        }
                    }
                }
            }
        }
    }
}
