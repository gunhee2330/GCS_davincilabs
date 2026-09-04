import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQml
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.PlanView

/// Mission item edit control
Rectangle {
    required property var    missionItem         ///< MissionItem associated with this editor
    required property var    map                 ///< Map control

    signal clicked
    signal remove
    signal selectNextNotReadyItem
    signal editorExpandedAndLoaded

    id:             _root
    height:         _currentItem ? (editorLoader.y + editorLoader.height + _innerMargin) : (topRowLayout.y + topRowLayout.height + _margin)
    // 선택 항목을 buttonHighlight 로 통째로 칠하면 목록에서 뜯겨 나온 창처럼 보인다.
    // 옅은 배경 + 좌측 강조 막대로 바꿔 펼쳐진 세부사항이 목록의 일부로 이어지게 한다.
    color:          _currentItem ? Qt.rgba(_accent.r, _accent.g, _accent.b, 0.14) : qgcPal.window
    radius:         0
    opacity:        1.0
    border.width:   _readyForSave ? 0 : 2
    border.color:   qgcPal.warningText

    property var    _masterController:          missionItem.masterController
    property var    _missionController:         _masterController.missionController
    property bool   _currentItem:               missionItem.isCurrentItem
    /// 경찰 강조색. 순정 하늘색(buttonHighlight)을 쓰지 않는다.
    readonly property color _accent: _lightTheme ? PolicePalette.accentLight : PolicePalette.accentDark
    // 배경이 더 이상 buttonHighlight 로 차지 않으므로 그 위의 글자색도 평상시 색을 쓴다.
    property color  _outerTextColor:            qgcPal.text
    property bool   _noMissionItemsAdded:       _missionController.visualItems ? _missionController.visualItems.count <= 1 : true
    property real   _sectionSpacer:             ScreenTools.defaultFontPixelWidth / 2  // spacing between section headings
    property bool   _singleComplexItem:         _missionController.complexMissionItems.length === 1
    property bool   _readyForSave:              missionItem.readyForSaveState === VisualMissionItem.ReadyForSave
    property bool   _lightTheme:                qgcPal.globalTheme === QGCPalette.Light

    readonly property real  _editFieldWidth:    Math.min(width - _innerMargin * 2, ScreenTools.defaultFontPixelWidth * 12)
    readonly property real  _margin:            ScreenTools.defaultFontPixelWidth / 2
    readonly property real  _innerMargin:       0
    readonly property real  _radius:            ScreenTools.defaultFontPixelWidth / 2
    // 커맨드 행을 터치 크기로 키우면서 여기에 연동해 두면 아이콘까지 2배가 된다. 따로 고정한다.
    readonly property real  _hamburgerSize:     ScreenTools.defaultFontPixelHeight * 1.75
    readonly property real  _trashSize:         ScreenTools.defaultFontPixelHeight * 1.75
    // 선택 강조 막대 두께. 아래 막대와 editorLoader 의 좌측 여백이 같은 값을 써야 편집기가
    // 막대에 덮이지 않는다. 3px 로는 야외에서 어느 행을 편집 중인지 못 찾는다(막대 대비 2.16:1).
    readonly property real  _accentWidth:       5
    readonly property bool  _waypointsOnlyMode: QGroundControl.corePlugin.options.missionWaypointsOnly

    // 글꼴 정책은 MissionStats.qml 상단 주석 참조. 4파일 공통, 나중에 한 곳으로 모을 것.
    readonly property real  _fontEmphasis:      ScreenTools.defaultFontPointSize * 1.15
    readonly property real  _fontCaption:       ScreenTools.smallFontPointSize

    // setSource() injects missionItem before internal bindings activate
    function _loadEditor() {
        if (missionItem.isCurrentItem) {
            editorLoader.setSource(missionItem.editorQml, {
                missionItem:    _root.missionItem,
                availableWidth: _root.width - (editorLoader.anchors.margins * 2)
            })
        } else {
            editorLoader.setSource("")
        }
    }

    Connections {
        target: missionItem
        function onIsCurrentItemChanged() { _root._loadEditor() }
    }

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: enabled
    }

    FocusScope {
        id:             currentItemScope
        anchors.fill:   parent

        MouseArea {
            anchors.fill:   parent
            onClicked: {
                if (mainWindow.allowViewSwitch()) {
                    currentItemScope.focus = true
                    _root.clicked()
                }
            }
        }
    }

    QGCPopupDialogFactory {
        id: editPositionDialogFactory

        dialogComponent: editPositionDialog
    }

    Component {
        id: editPositionDialog

        EditPositionDialog {
            property bool _editCenterCoordinate: false

            onCoordinateChanged: {
                if (_editCenterCoordinate)
                    missionItem.centerCoordinate = coordinate
                else
                    missionItem.coordinate = coordinate
            }
        }
    }

    Row {
        id:                 topRowLayout
        anchors.margins:    _margin
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            _margin

        Rectangle {
            id:                     notReadyForSaveIndicator
            anchors.verticalCenter: parent.verticalCenter
            width:                  _hamburgerSize
            height:                 width
            // 흰 원 + 빨간 물음표는 Dark 에서 글자 대비가 3.22:1 이고, Light 에서는 흰 배경 위
            // 흰 원이라 테두리 1px 말고는 아무것도 남지 않는다. 원을 경고색으로 채우고
            // 기호를 배경색으로 빼면 두 테마 모두 4.95:1 이상이 되고 테두리도 필요 없어진다.
            color:                  qgcPal.warningText
            radius:                 width / 2
            visible:                !_readyForSave

            QGCLabel {
                id:                 readyForSaveLabel
                anchors.centerIn:   parent
                //: Indicator in Plan view to show mission item is not ready for save/send
                text:               qsTr("?")
                color:              qgcPal.window
                // 원 지름의 약 62% 를 글자 높이로 잡는다. 원(28px)은 충분히 큰데 기호가
                // 캡션 크기면 잉크 높이가 0.89mm(안드로이드 1.25mm)라 원 안이 비어 보인다.
                font.pointSize:     notReadyForSaveIndicator.width * 0.62 *
                                        ScreenTools.defaultFontPointSize / ScreenTools.defaultFontPixelHeight
            }
        }

        // 지도 마커에 찍히는 번호를 목록 행과 편집기 헤더에도 같이 보인다. 지도가 쓰는 값은
        // sequenceNumber 다(MissionItemIndicator.qml:25, TransectStyleMapVisuals.qml:125).
        // 좌표를 갖지 않는 항목(속도 변경, 귀환 등)은 지도에 마커가 없어 짝지을 것이 없으므로 뺀다.
        // 0번은 계획 홈 위치이고 지도에서는 집 모양 아이콘이라(HomePositionMapVisual.qml) 번호가 없다.
        // 지도 마커와 짝이 맞아야 하므로 배지를 다시 그리지 않고 지도가 쓰는 것을 그대로 쓴다.
        // 크기·색·선택 강조(초록)·약어 규칙이 전부 한 곳에서 온다.
        // index 식은 MissionItemIndicator.qml:25 와 같아야 한다 — 약어가 알파벳으로 시작하면
        // 지도는 숫자 대신 그 글자를 찍는다(이륙 T, 착륙 L 등).
        MissionItemIndexLabel {
            id:                     sequenceBadge
            anchors.verticalCenter: parent.verticalCenter
            // 배지 안에 찍히는 것은 지도와 글자 하나까지 같아야 한다.
            // 아래 식은 MissionItemIndicator.qml:25 와 동일하다 — 약어가 알파벳으로 시작하면
            // 지도가 숫자 대신 그 글자를 찍는다. label 에 한 글자만 넘기는 이유는
            // MissionItemIndexLabel 이 label.length > 1 일 때만 배지 옆에 글자를 덧붙이기 때문이다.
            // 행에는 이미 항목 이름이 적혀 있으므로 그 덧글자는 필요 없다.
            label:                  _abbrevIsLatin ? missionItem.abbreviation.charAt(0) : ""
            index:                  _abbrevIsLatin ? -1 : missionItem.sequenceNumber

            // 복합 항목(구역 비행·선형 비행 등)은 지도가 언제나 sequenceNumber 만 찍는다
            // (TransectStyleMapVisuals.qml:124 등). 약어가 알파벳이라도 글자를 쓰면 지도와 어긋난다.
            readonly property bool _abbrevIsLatin: missionItem.isSimpleItem &&
                                                   missionItem.abbreviation.charAt(0) > 'A' &&
                                                   missionItem.abbreviation.charAt(0) < 'z'
            checked:                missionItem.isCurrentItem
            // MissionItemIndexLabel 은 자체 QGCMouseArea 를 품는다. 목록에서는 배지가 아니라
            // 행 전체가 눌려야 하므로 죽인다(enabled 는 자식에 전파된다). 그리기에는 영향 없다.
            enabled:                false
            // 이륙은 코퍼에서 specifiesCoordinate 가 false 다(발진 지점에서 뜨므로 고도만 지정).
            // 그래도 지도에는 전용 비주얼로 번호가 찍히므로 목록에도 번호를 준다.
            visible:                missionItem.sequenceNumber !== 0 &&
                                        (missionItem.specifiesCoordinate || missionItem.isTakeoffItem)
        }

        QGCColoredImage {
            id:                     deleteButton
            anchors.verticalCenter: parent.verticalCenter
            height:                 _hamburgerSize
            width:                  height
            sourceSize.height:      height
            fillMode:               Image.PreserveAspectFit
            mipmap:                 true
            smooth:                 true
            color:                  _outerTextColor
            visible:                _currentItem && missionItem.sequenceNumber !== 0
            source:                 "/res/TrashDelete.svg"

            QGCMouseArea {
                fillItem:   parent
                onClicked:  remove()
            }
        }

        Item {
            id:                     commandPicker
            anchors.verticalCenter: parent.verticalCenter
            // 7인치 터치(1mm = 8.49px): 목록에서 가장 자주 눌리는 대상이 이 커맨드 선택 행이다.
            // 기본 26px(3.1mm) → 52px(6.1mm). 위아래 _margin 까지 더해 항목 행 전체는 60px(7.1mm).
            height:                 Math.max(ScreenTools.implicitComboBoxHeight, ScreenTools.defaultFontPixelHeight * 3.25)
            width:                  innerLayout.width
            visible:                !commandLabel.visible

            RowLayout {
                id:                     innerLayout
                anchors.verticalCenter: parent.verticalCenter
                spacing:                _padding

                property real _padding: ScreenTools.comboBoxPadding

                QGCLabel {
                    text:           missionItem.commandName
                    // 본문 13px(1.5mm) → 15px(1.8mm)
                    font.pointSize: _root._fontEmphasis
                }

                QGCColoredImage {
                    height:             ScreenTools.defaultFontPixelWidth
                    width:              height
                    fillMode:           Image.PreserveAspectFit
                    smooth:             true
                    antialiasing:       true
                    color:              qgcPal.text
                    source:             "/qmlimages/arrow-down.png"
                }
            }

            QGCMouseArea {
                fillItem:   parent
                onClicked:  commandDialogFactory.open()
            }

            QGCPopupDialogFactory {
                id: commandDialogFactory

                dialogComponent: commandDialog
            }

            Component {
                id: commandDialog

                MissionCommandDialog {
                    vehicle:                    _masterController.controllerVehicle
                    missionItem:                _root.missionItem
                    map:                        _root.map
                    // FIXME: Disabling fly through commands doesn't work since you may need to change from an RTL to something else
                    flyThroughCommandsAllowed:  true //_missionController.flyThroughCommandsAllowed
                }
            }
        }

        QGCLabel {
            id:                     commandLabel
            anchors.verticalCenter: parent.verticalCenter
            width:                  commandPicker.width
            height:                 commandPicker.height
            visible:                !missionItem.isCurrentItem || !missionItem.isSimpleItem || _waypointsOnlyMode || missionItem.isTakeoffItem
            verticalAlignment:      Text.AlignVCenter
            text:                   missionItem.commandName
            color:                  _outerTextColor
            // 본문 13px(1.5mm) → 15px(1.8mm)
            font.pointSize:         _root._fontEmphasis
        }
    }

    Component {
        id: hamburgerMenuDropPanelComponent

        DropPanel {
            id: hamburgerMenuDropPanel
            onClosed: destroy()

            sourceComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelHeight / 2

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Move to vehicle position")
                        enabled:            _activeVehicle && missionItem.specifiesCoordinate && _activeVehicle.coordinate.isValid

                        onClicked: {
                            missionItem.coordinate = _activeVehicle.coordinate
                            hamburgerMenuDropPanel.close()
                        }

                        property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Move to previous item position")
                        enabled:            _missionController.previousCoordinate.isValid
                        onClicked: {
                            missionItem.coordinate = _missionController.previousCoordinate
                            hamburgerMenuDropPanel.close()
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Edit position...")
                        enabled:            missionItem.specifiesCoordinate
                        onClicked: {
                            const editCenterCoordinate = missionItem.isSurveyItem
                            editPositionDialogFactory.open({
                                _editCenterCoordinate:   editCenterCoordinate,
                                coordinate:              editCenterCoordinate ? missionItem.centerCoordinate : missionItem.coordinate,
                                altitudeFact:            !editCenterCoordinate && missionItem.specifiesAltitude ? missionItem.altitude : null,
                                altitudeFrame:           !editCenterCoordinate && missionItem.specifiesAltitude ? missionItem.altitudeFrame : QGroundControl.AltitudeFrameNone,
                            })
                            hamburgerMenuDropPanel.close()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth:       true
                        Layout.preferredHeight: 1
                        color:                  qgcPal.groupBorder
                    }

                    QGCCheckBoxSlider {
                        Layout.fillWidth:   true
                        text:               qsTr("Show all values")
                        visible:            QGroundControl.corePlugin.showAdvancedUI
                        checked:            missionItem.isSimpleItem ? missionItem.rawEdit : false
                        enabled:            missionItem.isSimpleItem && !_waypointsOnlyMode

                        onClicked: {
                            missionItem.rawEdit = checked
                            if (missionItem.rawEdit && !missionItem.friendlyEditAllowed) {
                                missionItem.rawEdit = false
                                checked = false
                                QGroundControl.showMessageDialog(_root, qsTr("Mission Edit"), qsTr("You have made changes to the mission item which cannot be shown in Simple Mode"))
                            }
                            hamburgerMenuDropPanel.close()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth:       true
                        Layout.preferredHeight: 1
                        color:                  qgcPal.groupBorder
                    }

                    QGCLabel {
                        text:       qsTr("Item #%1").arg(missionItem.sequenceNumber)
                        enabled:    false
                    }
                }
            }
        }
    }

    QGCColoredImage {
        id:                     hamburger
        anchors.margins:        _margin
        anchors.right:          parent.right
        anchors.verticalCenter: topRowLayout.verticalCenter
        width:                  _hamburgerSize
        height:                 _hamburgerSize
        sourceSize.height:      _hamburgerSize
        source:                 "qrc:/qmlimages/Hamburger.svg"
        visible:                missionItem.isCurrentItem && missionItem.sequenceNumber !== 0
        color:                  _outerTextColor

        QGCMouseArea {
            fillItem:   hamburger
            onClicked: (position) => {
                currentItemScope.focus = true
                position = Qt.point(position.x, position.y)
                // For some strange reason using mainWindow in mapToItem doesn't work, so we use globals.parent instead which also gets us mainWindow
                position = mapToItem(globals.parent, position)

                var dropPanel = hamburgerMenuDropPanelComponent.createObject(mainWindow, { clickRect: Qt.rect(position.x, position.y, 0, 0) })
                dropPanel.open()
            }
        }
    }

    /*
    QGCLabel {
        id:                     notReadyForSaveLabel
        anchors.margins:        _margin
        anchors.left:           notReadyForSaveIndicator.right
        anchors.right:          parent.right
        anchors.top:            commandPicker.bottom
        visible:                _currentItem && !_readyForSave
        text:                   missionItem.readyForSaveState === VisualMissionItem.NotReadyForSaveTerrain ?
                                    qsTr("Incomplete: Waiting on terrain data.") :
                                    qsTr("Incomplete: Item not fully specified.")
        wrapMode:               Text.WordWrap
        horizontalAlignment:    Text.AlignHCenter
        color:                  qgcPal.warningText
    }

*/

    Loader {
        id:                 editorLoader
        anchors.margins:    _innerMargin
        anchors.left:       parent.left
        // 좌측 강조 막대 자리를 비워 둔다. 안 비우면 편집기 라벨 왼쪽이 막대에 덮인다.
        anchors.leftMargin: _accentWidth
        anchors.top:        topRowLayout.bottom

        // Deliberately not asynchronous. With asynchronous: true the editor is built
        // incrementally over later event-loop ticks. If the user switches layers before
        // that finishes, the TreeView collapses the mission group and destroys this
        // delegate, and the still-running load then warns "Cannot create a component in
        // an invalid context". Synchronous loading closes that window: the editor is
        // fully built before control returns to the event loop.
        Component.onCompleted: _root._loadEditor()
    }

    Rectangle {
        anchors.left:   parent.left
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        width:          _accentWidth
        z:              100
        // 배경 강조(알파 0.12)는 어떤 조합에서도 1.09~1.18:1 이라 선택 표시를 떠받치지 못한다.
        // 알파를 올리면 사용자가 거부한 '파란 상자' 로 되돌아가므로 막대만 강하게 한다.
        // 순정 하늘색은 Light + 어두운 지도(패널 알파 0.85) 위에서 2.16:1 로 무너진다.
        // Light 에서만 경찰 남색으로 바꿔 9.39:1 로 올린다(Dark 는 3.25~5.54:1 로 이미 3:1 이상).
        color:          _accent
        visible:        _currentItem
    }

    onHeightChanged: {
        if (_currentItem && editorLoader.status === Loader.Ready) {
            _editorHeightSettleTimer.restart()
        }
    }

    Timer {
        id: _editorHeightSettleTimer
        interval: 100
        onTriggered: _root.editorExpandedAndLoaded()
    }
}
