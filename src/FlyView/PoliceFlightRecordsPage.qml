import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// 비행 기록: every airframe's flights for a span of days, opened from the status drawer's 전체 기록.
///
/// The procurement spec wants the flight log easy to read and handed over for any period on
/// request, so the records are listed with a summary of the span, exported with their log files
/// to the save folder or a USB stick, and each can fetch the aircraft's own log. Shown in the tool
/// drawer under the settings screens' bar, drawn in the settings look after the police mockup,
/// where 1 px of the 1920 wide mockup is _px(1).
Rectangle {
    id:         page
    objectName: "policeFlightRecordsPage"
    color:      qgcPal.settingsPanel

    /// The settings screens' controls under here: their boxes, text size and touch targets
    readonly property bool settingsMockupLook: true

    /// Days the list shows, yyyy-MM-dd, as last applied with 조회
    property string _from: ""
    property string _to:   ""
    property var    _records: []
    /// Rows ticked for export, by _key(); _checkedRevision re-evaluates what reads it
    property var    _checked: ({})
    property int    _checkedRevision: 0
    /// The row the right pane describes
    property int    _current: 0
    property string _message: ""
    property bool   _messageIsError: false

    readonly property var _selected: (_current >= 0 && _current < _records.length) ? _records[_current] : null
    readonly property var _checkedRecords: {
        void _checkedRevision
        return _records.filter(record => !!_checked[_key(record)])
    }
    readonly property var  _download:    App.FlightRecords.download
    readonly property bool _downloadingHere: !!_selected && !!_download.busy && _download.airframe === _selected.airframe && _download.takeoff === _selected.takeoff
    readonly property bool _failedHere:  !!_selected && !_download.busy && !!_download.error && _download.airframe === _selected.airframe && _download.takeoff === _selected.takeoff
    /// The mockup's column widths, the tick column as wide as the stock check box needs and the
    /// airframe's as wide as a three digit system id
    readonly property var  _columns:     [ _px(86), _px(272), _px(118), _px(132), _px(140), _px(110), _px(110) ]

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    function _px(mockupPixels) {
        return ScreenTools.mockupUnit * mockupPixels / 19.2
    }

    function _key(record) {
        return record.airframe + "/" + record.takeoff
    }

    function _validDate(text) {
        return /^\d{4}-\d\d-\d\d$/.test(text) && !isNaN(Date.fromLocaleString(Qt.locale(), text, "yyyy-MM-dd").getTime())
    }

    function _query() {
        const shown = _selected ? _key(_selected) : ""
        _records = App.FlightRecords.records(_from, _to)
        const index = _records.findIndex(record => _key(record) === shown)
        _current = index >= 0 ? index : 0
        _checkedRevision++
    }

    function _apply() {
        fromField.validationError = !_validDate(fromField.text)
        toField.validationError = !_validDate(toField.text)
        if (fromField.validationError || toField.validationError) {
            return
        }
        _from = fromField.text
        _to = toField.text
        _query()
    }

    function _setChecked(record, checked) {
        _checked[_key(record)] = checked
        _checkedRevision++
    }

    function _checkAll(checked) {
        for (const record of _records) {
            _checked[_key(record)] = checked
        }
        _checkedRevision++
    }

    /// The ticked rows, or every listed one when none is ticked, to \a parentDir
    function _export(parentDir) {
        if (parentDir === "") {
            _message = qsTr("USB 저장장치가 연결되어 있지 않습니다")
            _messageIsError = true
            return
        }
        const records = _checkedRecords.length > 0 ? _checkedRecords : _records
        const folder = App.FlightRecords.exportRecords(records, parentDir)
        _messageIsError = folder === ""
        _message = _messageIsError ? qsTr("내보내지 못했습니다") : qsTr("%1건을 내보냈습니다: %2").arg(records.length).arg(folder)
    }

    function _durationText(total) {
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        return (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    function _hoursText(total) {
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        return h > 0 ? qsTr("%1시간 %2분").arg(h).arg(m) : qsTr("%1분").arg(m)
    }

    /// 9월 1일부터 28일까지: the second day drops what it shares with the first
    function _periodText() {
        const f = _from.split("-").map(Number)
        const t = _to.split("-").map(Number)
        if (f.length !== 3 || t.length !== 3) {
            return ""
        }
        const fromText = (f[0] !== t[0] ? qsTr("%1년 ").arg(f[0]) : "") + qsTr("%1월 %2일").arg(f[1]).arg(f[2])
        const toText = f[0] !== t[0] ? qsTr("%1년 %2월 %3일").arg(t[0]).arg(t[1]).arg(t[2])
                     : f[1] !== t[1] ? qsTr("%1월 %2일").arg(t[1]).arg(t[2])
                                     : qsTr("%1일").arg(t[2])
        return qsTr("%1부터 %2까지").arg(fromText).arg(toText)
    }

    /// Local ISO 8601, cut rather than parsed, as the drawer does
    function _dateTimeText(iso) {
        return iso ? iso.replace("T", " ") : "—"
    }

    /// The landing's time alone when it is the takeoff's day, as the column is narrow
    function _landingText(record) {
        if (!record.landing) {
            return "—"
        }
        return record.landing.substring(0, 10) === record.takeoff.substring(0, 10) ? record.landing.substring(11, 19)
                                                                                   : record.landing.substring(5, 19).replace("T", " ")
    }

    function _altitudeText(record) {
        return record.maxAltitude === undefined ? "—" : QGroundControl.unitsConversion.metersToAppSettingsVerticalDistanceUnitsString(record.maxAltitude, 0)
    }

    readonly property string _summary: {
        let seconds = 0
        let metres = 0
        for (const record of _records) {
            seconds += record.seconds
            metres += record.metres
        }
        const parts = [ _periodText() + " " + qsTr("%1건").arg(_records.length),
                        qsTr("총 비행 시간 %1").arg(_hoursText(seconds)),
                        qsTr("총 거리 %1").arg(QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnitsString(metres)) ]
        if (_checkedRecords.length > 0) {
            parts.push(qsTr("%1건 선택").arg(_checkedRecords.length))
        }
        return parts.join("    ")
    }

    Component.onCompleted: {
        // The last thirty days, today included
        const today = new Date()
        toField.text = Qt.formatDate(today, "yyyy-MM-dd")
        fromField.text = Qt.formatDate(new Date(today.getFullYear(), today.getMonth(), today.getDate() - 29), "yyyy-MM-dd")
        _apply()
    }

    Connections {
        target: App.FlightRecords
        function onRecordsChanged() { page._query() }
    }

    // This needs to block click event leakage to the view underneath.
    DeadMouseArea {
        anchors.fill: parent
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

    component Rule: Rectangle {
        Layout.fillWidth:    true
        implicitHeight:      1
        color:               _rulePal.cardBorder
        opacity:             0.6
        QGCPalette { id: _rulePal; colorGroupEnabled: true }
    }

    /// A log file's name behind a page icon, or an em dash without one
    component FileName: RowLayout {
        property alias fileName:  _fileLabel.text
        property alias pointSize: _fileLabel.font.pointSize
        spacing:            ScreenTools.mockupUnit * 10 / 19.2
        // A layout fills its column's height unless told not to
        Layout.fillHeight:  false

        QGCPalette { id: _filePal; colorGroupEnabled: true }

        QGCColoredImage {
            Layout.preferredWidth:  ScreenTools.mockupUnit * 26 / 19.2
            Layout.preferredHeight: ScreenTools.mockupUnit * 26 / 19.2
            source:                 "/InstrumentValueIcons/document.svg"
            color:                  _filePal.secondaryText
            visible:                _fileLabel.text !== "—"
        }

        QGCLabel {
            id:                 _fileLabel
            Layout.fillWidth:   true
            font.pointSize:     ScreenTools.defaultFontPointSize * 0.85
            elide:              Text.ElideMiddle
        }
    }

    ColumnLayout {
        anchors.fill:           parent
        anchors.leftMargin:     page._px(40)
        anchors.rightMargin:    page._px(40)
        anchors.topMargin:      page._px(22)
        anchors.bottomMargin:   page._px(14)
        spacing:                page._px(10)

        Caption { text: qsTr("조회 기간") }

        Card {
            Layout.fillWidth:       true
            Layout.preferredHeight: page._px(110)

            RowLayout {
                anchors.fill:           parent
                anchors.leftMargin:     page._px(27)
                anchors.rightMargin:    page._px(27)
                spacing:                page._px(18)

                QGCTextField {
                    id:                     fromField
                    objectName:             "flightRecordsFrom"
                    Layout.preferredWidth:  page._px(240)
                    Layout.preferredHeight: page._px(57)
                    inputMask:              "9999-99-99"
                    inputMethodHints:       Qt.ImhDigitsOnly
                    onAccepted:             page._apply()
                }

                QGCLabel {
                    text:  "~"
                    color: qgcPal.secondaryText
                }

                QGCTextField {
                    id:                     toField
                    objectName:             "flightRecordsTo"
                    Layout.preferredWidth:  page._px(240)
                    Layout.preferredHeight: page._px(57)
                    inputMask:              "9999-99-99"
                    inputMethodHints:       Qt.ImhDigitsOnly
                    onAccepted:             page._apply()
                }

                QGCButton {
                    objectName:             "flightRecordsQuery"
                    Layout.leftMargin:      page._px(8)
                    Layout.preferredHeight: page._px(64)
                    text:                   qsTr("조회")
                    primary:                true
                    onClicked:              page._apply()
                }

                Item { Layout.fillWidth: true }

                QGCButton {
                    objectName:             "flightRecordsExport"
                    Layout.preferredHeight: page._px(64)
                    text:                   qsTr("선택 내보내기 (기록과 로그)")
                    enabled:                page._records.length > 0
                    onClicked:              page._export(App.FlightRecords.exportDirectory())
                }

                QGCButton {
                    objectName:             "flightRecordsUsb"
                    Layout.preferredHeight: page._px(64)
                    text:                   qsTr("USB로 저장")
                    enabled:                page._records.length > 0
                    onClicked:              page._export(App.FlightRecords.usbDirectory())
                }
            }
        }

        Caption {
            objectName:         "flightRecordsSummary"
            Layout.topMargin:   page._px(16)
            text:               page._summary
        }

        RowLayout {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            spacing:            page._px(24)

            // The list: a header over one row per flight, newest first
            Card {
                Layout.fillWidth:   true
                Layout.fillHeight:  true
                clip:               true

                ColumnLayout {
                    anchors.fill:       parent
                    anchors.margins:    ScreenTools.hairline
                    spacing:            0

                    RowLayout {
                        Layout.fillWidth:       true
                        Layout.fillHeight:      false
                        Layout.preferredHeight: page._px(60)
                        spacing:                0

                        Item {
                            Layout.preferredWidth:  page._columns[0]
                            Layout.fillHeight:      true

                            QGCCheckBox {
                                id:                     checkAll
                                objectName:             "flightRecordsCheckAll"
                                anchors.left:           parent.left
                                anchors.leftMargin:     page._px(24)
                                anchors.verticalCenter: parent.verticalCenter
                                checked:                page._records.length > 0 && page._checkedRecords.length === page._records.length
                                onClicked: {
                                    page._checkAll(checked)
                                    checked = Qt.binding(() => page._records.length > 0 && page._checkedRecords.length === page._records.length)
                                }
                            }
                        }

                        Repeater {
                            model: [ qsTr("이륙 일시"), qsTr("착륙"), qsTr("비행 시간"), qsTr("비행 거리"), qsTr("최대 고도"), qsTr("기체") ]

                            QGCLabel {
                                required property string modelData
                                required property int    index
                                Layout.preferredWidth:  page._columns[index + 1]
                                text:                   modelData
                                font.pointSize:         ScreenTools.defaultFontPointSize * 0.8
                                color:                  qgcPal.secondaryText
                            }
                        }

                        QGCLabel {
                            Layout.fillWidth:   true
                            text:               qsTr("로그")
                            font.pointSize:     ScreenTools.defaultFontPointSize * 0.8
                            color:              qgcPal.secondaryText
                        }
                    }

                    QGCListView {
                        id:                 table
                        objectName:         "flightRecordsTable"
                        Layout.fillWidth:   true
                        Layout.fillHeight:  true
                        model:              page._records

                        QGCLabel {
                            anchors.centerIn:   parent
                            visible:            table.count === 0
                            text:               qsTr("이 기간에 기록이 없습니다")
                            color:              qgcPal.secondaryText
                        }

                        delegate: Rectangle {
                            id:         row
                            objectName: "flightRecordsRow_" + index
                            width:      ListView.view.width
                            height:     page._px(76)
                            color:      index === page._current ? qgcPal.selectedRow : "transparent"

                            required property var modelData
                            required property int index

                            Rule { width: parent.width }

                            Rectangle {
                                anchors.left:   parent.left
                                anchors.top:    parent.top
                                anchors.bottom: parent.bottom
                                width:          page._px(5)
                                color:          qgcPal.buttonHighlight
                                visible:        row.index === page._current
                            }

                            MouseArea {
                                anchors.fill:   parent
                                onClicked:      page._current = row.index
                            }

                            RowLayout {
                                anchors.fill:   parent
                                spacing:        0

                                Item {
                                    Layout.preferredWidth:  page._columns[0]
                                    Layout.fillHeight:      true

                                    QGCCheckBox {
                                        objectName:             "flightRecordsCheck_" + row.index
                                        anchors.left:           parent.left
                                        anchors.leftMargin:     page._px(24)
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked:                { void page._checkedRevision; return !!page._checked[page._key(row.modelData)] }
                                        onClicked: {
                                            page._setChecked(row.modelData, checked)
                                            checked = Qt.binding(() => { void page._checkedRevision; return !!page._checked[page._key(row.modelData)] })
                                        }
                                    }
                                }

                                Repeater {
                                    model: [ page._dateTimeText(row.modelData.takeoff),
                                             page._landingText(row.modelData),
                                             page._durationText(row.modelData.seconds),
                                             QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnitsString(row.modelData.metres),
                                             page._altitudeText(row.modelData),
                                             row.modelData.vehicle || "—" ]

                                    QGCLabel {
                                        required property string modelData
                                        required property int    index
                                        Layout.preferredWidth:  page._columns[index + 1]
                                        text:                   modelData
                                        font.pointSize:         ScreenTools.defaultFontPointSize * 0.9
                                        elide:                  Text.ElideRight
                                    }
                                }

                                FileName {
                                    Layout.fillWidth:   true
                                    Layout.rightMargin: page._px(16)
                                    fileName:           row.modelData.tlog || "—"
                                    pointSize:          ScreenTools.defaultFontPointSize * 0.8
                                }
                            }
                        }
                    }
                }
            }

            // The chosen flight in full, and its vehicle log
            Card {
                objectName:             "flightRecordsDetail"
                Layout.preferredWidth:  page._px(526)
                Layout.fillHeight:      true

                ColumnLayout {
                    anchors.fill:       parent
                    anchors.margins:    page._px(24)
                    spacing:            page._px(8)

                    Caption { text: qsTr("선택한 비행") }

                    QGCLabel {
                        Layout.bottomMargin:    page._px(14)
                        visible:                !!page._selected
                        text: {
                            if (!page._selected) {
                                return ""
                            }
                            const t = page._selected.takeoff
                            return qsTr("%1월 %2일 %3 비행").arg(Number(t.substring(5, 7))).arg(Number(t.substring(8, 10))).arg(t.substring(11, 16))
                        }
                        font.pointSize: ScreenTools.defaultFontPointSize * 1.07
                        font.bold:      true
                    }

                    GridLayout {
                        Layout.fillWidth:   true
                        Layout.fillHeight:  false
                        columns:            2
                        columnSpacing:      0
                        rowSpacing:         page._px(12)
                        visible:            !!page._selected

                        Repeater {
                            model: page._selected ? [
                                qsTr("이륙 일시"), page._dateTimeText(page._selected.takeoff),
                                qsTr("착륙 일시"), page._dateTimeText(page._selected.landing),
                                qsTr("비행 시간"), page._durationText(page._selected.seconds),
                                qsTr("비행 거리"), QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnitsString(page._selected.metres),
                                qsTr("최대 고도"), page._altitudeText(page._selected),
                                qsTr("기체"), page._selected.vehicle || "—" ] : []

                            QGCLabel {
                                required property string modelData
                                required property int    index
                                Layout.preferredWidth:  index % 2 === 0 ? page._px(150) : -1
                                Layout.fillWidth:       index % 2 === 1
                                text:                   modelData
                                font.pointSize:         ScreenTools.defaultFontPointSize * 0.9
                                color:                  index % 2 === 0 ? qgcPal.secondaryText : qgcPal.text
                            }
                        }
                    }

                    Rule {
                        Layout.topMargin:       page._px(14)
                        Layout.bottomMargin:    page._px(14)
                        visible:                !!page._selected
                    }

                    Caption {
                        text:           qsTr("텔레메트리 로그")
                        font.pointSize: ScreenTools.defaultFontPointSize * 0.8
                        visible:        !!page._selected
                    }

                    FileName {
                        Layout.fillWidth:   true
                        visible:            !!page._selected
                        fileName:           page._selected && page._selected.tlog ? page._selected.tlog : "—"
                    }

                    Caption {
                        Layout.topMargin:   page._px(14)
                        text:               qsTr("기체 로그 상태")
                        font.pointSize:     ScreenTools.defaultFontPointSize * 0.8
                        visible:            !!page._selected
                    }

                    // 받음 with the file, 받는 중 with how far, or 받기 전
                    ColumnLayout {
                        id:                 logState
                        objectName:         "flightRecordsLogState"
                        Layout.fillWidth:   true
                        Layout.fillHeight:  false
                        spacing:            page._px(12)
                        visible:            !!page._selected

                        readonly property bool received: !!page._selected && !!page._selected.vehicleLog
                        readonly property string stateText: received ? qsTr("받음")
                                                            : page._downloadingHere ? qsTr("받는 중 %1%").arg(page._download.percent)
                                                                                    : qsTr("받기 전")

                        Rectangle {
                            visible:        logState.received
                            implicitWidth:  receivedLabel.implicitWidth + page._px(24)
                            implicitHeight: page._px(34)
                            radius:         page._px(6)
                            color:          Qt.darker(qgcPal.colorGreen, 2.2)

                            QGCLabel {
                                id:                 receivedLabel
                                anchors.centerIn:   parent
                                text:               logState.stateText
                                color:              "white"
                                font.bold:          true
                                font.pointSize:     ScreenTools.defaultFontPointSize * 0.75
                            }
                        }

                        FileName {
                            Layout.fillWidth:   true
                            visible:            logState.received
                            fileName:           logState.received ? page._selected.vehicleLog : "—"
                        }

                        RowLayout {
                            Layout.fillWidth:   true
                            Layout.fillHeight:  false
                            spacing:            page._px(16)
                            visible:            !logState.received

                            QGCLabel {
                                text:           logState.stateText
                                font.pointSize: ScreenTools.defaultFontPointSize * 0.93
                            }

                            Rectangle {
                                Layout.fillWidth:       true
                                Layout.preferredHeight: page._px(8)
                                radius:                 height / 2
                                color:                  Qt.rgba(qgcPal.cardBorder.r, qgcPal.cardBorder.g, qgcPal.cardBorder.b, 0.5)
                                visible:                page._downloadingHere

                                Rectangle {
                                    width:  parent.width * page._download.percent / 100
                                    height: parent.height
                                    radius: parent.radius
                                    color:  qgcPal.buttonHighlight
                                }
                            }
                        }

                        QGCLabel {
                            Layout.fillWidth:   true
                            visible:            !logState.received && (page._failedHere || !QGroundControl.multiVehicleManager.activeVehicle)
                            text:               page._failedHere ? page._download.error : qsTr("기체를 연결하면 받을 수 있습니다")
                            color:              page._failedHere ? qgcPal.colorOrange : qgcPal.secondaryText
                            font.pointSize:     ScreenTools.defaultFontPointSize * 0.8
                            wrapMode:           Text.WordWrap
                        }
                    }

                    Item { Layout.fillHeight: true }

                    QGCButton {
                        objectName:             "flightRecordsDownload"
                        Layout.fillWidth:       true
                        Layout.preferredHeight: page._px(64)
                        text:                   qsTr("기체 로그 받기")
                        primary:                true
                        visible:                !!page._selected && !page._selected.vehicleLog
                        enabled:                !page._download.busy && !!QGroundControl.multiVehicleManager.activeVehicle
                        onClicked:              App.FlightRecords.downloadVehicleLog(page._selected)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth:   true
            Layout.fillHeight:  false
            spacing:            page._px(24)

            Caption { text: qsTr("기록은 개수 제한 없이 모두 보관합니다.") }

            QGCLabel {
                objectName:             "flightRecordsMessage"
                Layout.fillWidth:       true
                horizontalAlignment:    Text.AlignRight
                elide:                  Text.ElideMiddle
                text:                   page._message
                color:                  page._messageIsError ? qgcPal.colorOrange : qgcPal.text
                font.pointSize:         ScreenTools.defaultFontPointSize * 0.85
            }
        }
    }
}
