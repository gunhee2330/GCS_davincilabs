import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQml
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

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
    color:          _currentItem ? Qt.rgba(qgcPal.buttonHighlight.r, qgcPal.buttonHighlight.g, qgcPal.buttonHighlight.b, 0.12) : qgcPal.window
    radius:         0
    opacity:        1.0
    border.width:   _readyForSave ? 0 : 2
    border.color:   qgcPal.warningText

    property var    _masterController:          missionItem.masterController
    property var    _missionController:         _masterController.missionController
    property bool   _currentItem:               missionItem.isCurrentItem
    // 배경이 더 이상 buttonHighlight 로 차지 않으므로 그 위의 글자색도 평상시 색을 쓴다.
    property color  _outerTextColor:            qgcPal.text
    property bool   _noMissionItemsAdded:       _missionController.visualItems ? _missionController.visualItems.count <= 1 : true
    property real   _sectionSpacer:             ScreenTools.defaultFontPixelWidth / 2  // spacing between section headings
    property bool   _singleComplexItem:         _missionController.complexMissionItems.length === 1
    property bool   _readyForSave:              missionItem.readyForSaveState === VisualMissionItem.ReadyForSave

    readonly property real  _editFieldWidth:    Math.min(width - _innerMargin * 2, ScreenTools.defaultFontPixelWidth * 12)
    readonly property real  _margin:            ScreenTools.defaultFontPixelWidth / 2
    readonly property real  _innerMargin:       0
    readonly property real  _radius:            ScreenTools.defaultFontPixelWidth / 2
    // 커맨드 행을 터치 크기로 키우면서 여기에 연동해 두면 아이콘까지 2배가 된다. 따로 고정한다.
    readonly property real  _hamburgerSize:     ScreenTools.defaultFontPixelHeight * 1.75
    readonly property real  _trashSize:         ScreenTools.defaultFontPixelHeight * 1.75
    readonly property bool  _waypointsOnlyMode: QGroundControl.corePlugin.options.missionWaypointsOnly

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
            border.width:           1
            border.color:           qgcPal.warningText
            color:                  "white"
            radius:                 width / 2
            visible:                !_readyForSave

            QGCLabel {
                id:                 readyForSaveLabel
                anchors.centerIn:   parent
                //: Indicator in Plan view to show mission item is not ready for save/send
                text:               qsTr("?")
                color:              qgcPal.warningText
                font.pointSize:     ScreenTools.smallFontPointSize
            }
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
                    font.pointSize: ScreenTools.defaultFontPointSize * 1.15
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
            font.pointSize:         ScreenTools.defaultFontPointSize * 1.15
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
        // 좌측 강조 막대(3px) 자리를 비워 둔다. 안 비우면 편집기 라벨 왼쪽이 막대에 덮인다.
        anchors.leftMargin: 3
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
        width:          3
        z:              100
        color:          qgcPal.buttonHighlight
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
