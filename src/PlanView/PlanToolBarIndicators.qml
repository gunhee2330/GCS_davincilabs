import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls
import QGroundControl.PlanView
import QGroundControl.FactControls

// Toolbar for Plan View
RowLayout {
    required property var planMasterController
    property bool showRallyPointsHelp: false

    signal toolbarButtonClicked()

    id: root
    spacing: ScreenTools.defaultFontPixelWidth

    property var _planMasterController: planMasterController
    property var _missionController: _planMasterController.missionController
    property var _geoFenceController: _planMasterController.geoFenceController
    property var _rallyPointController: _planMasterController.rallyPointController
    property bool _controllerOffline: _planMasterController.offline
    property var _saveDirty: _planMasterController.dirtyForSave
    property var _uploadDirty: _planMasterController.dirtyForUpload
    property var _syncInProgress: _planMasterController.syncInProgress
    property var _visualItems: _missionController.visualItems
    property bool _hasPlanItems: _planMasterController.containsItems

    readonly property real _margins: ScreenTools.defaultFontPixelWidth

    function _uploadClicked() {
        _planMasterController.upload()
    }

    function _downloadClicked() {
        if (_saveDirty) {
            QGroundControl.showMessageDialog(root, qsTr("Download"),
                                         qsTr("You have unsaved changes. Downloading from the Vehicle will lose these changes. Are you sure?"),
                                         Dialog.Yes | Dialog.Cancel,
                                         function() { _planMasterController.loadFromVehicle() })
        } else {
            _planMasterController.loadFromVehicle()
        }
    }

    function _openButtonClicked() {
        // Unsent changes don't matter when offline or when the plan is safely saved to a file
        let planSafeOnDisk = !_saveDirty && _planMasterController.currentPlanFile !== ""
        let unsentChanges = _uploadDirty && !_controllerOffline && !planSafeOnDisk
        if (_saveDirty || unsentChanges) {
            let msg
            if (_saveDirty && unsentChanges) {
                msg = qsTr("You have unsaved/unsent changes. Loading a new Plan will lose these changes. Are you sure?")
            } else if (_saveDirty) {
                msg = qsTr("You have unsaved changes. Loading a new Plan will lose these changes. Are you sure?")
            } else {
                msg = qsTr("You have unsent changes. Loading a new Plan will lose these changes. Are you sure?")
            }
            QGroundControl.showMessageDialog(root, qsTr("Open Plan"),
                                        msg,
                                        Dialog.Yes | Dialog.Cancel,
                                        function() { _planMasterController.loadFromSelectedFile() } )
        } else {
            _planMasterController.loadFromSelectedFile()
        }
    }

    function _saveButtonClicked() {
        if (_planMasterController.currentPlanFile === "") {
            _planMasterController.saveToSelectedFile()
        } else {
            _planMasterController.saveToCurrent()
        }
    }

    function _saveAsKMLClicked() {
        // Don't save if we only have Mission Settings item
        if (_visualItems.count > 1) {
            _planMasterController.saveKmlToSelectedFile()
        }
    }

    function _storageClearButtonClicked() {
        QGroundControl.showMessageDialog(root, qsTr("Clear"),
                                     qsTr("Are you sure you want to remove all the items from the plan editor?"),
                                     Dialog.Yes | Dialog.Cancel,
                                     function() { _planMasterController.removeAll(); })
    }

    function _vehicleClearButtonClicked() {
        QGroundControl.showMessageDialog(root, qsTr("Clear"),
                                     qsTr("Are you sure you want to remove the plan from the vehicle and the plan editor?"),
                                     Dialog.Yes | Dialog.Cancel,
                                     function() {
                                        _planMasterController.removeAllFromVehicle()
                                     })
    }

    function _clearClicked() {
        if (_planMasterController.offline) {
            _storageClearButtonClicked();
        } else {
            _vehicleClearButtonClicked();
        }
    }

    QGCPalette { id: qgcPal }

    readonly property bool  _lightTheme: qgcPal.globalTheme === QGCPalette.Light
    readonly property color _accent:     _lightTheme ? PolicePalette.accentLight : PolicePalette.accentDark

    /// 순정 툴바 버튼은 둥근 모서리에 qgcPal.button(#626270, 보랏빛 도는 회색)으로 채워져 있다.
    /// 그 두 가지가 이 줄을 QGC 로 읽히게 한다. 채움과 둥근 모서리를 빼고 글자와 아이콘만 남긴다.
    /// 좌측 툴스트립을 이미 같은 방식으로 정리해 놓아 화면 안에서 일관된다.
    component ToolButton: QGCButton {
        backRadius:      0
        showBorder:      false
        backgroundColor: pressed  ? Qt.rgba(_accent.r, _accent.g, _accent.b, 0.30)
                                  : hovered ? Qt.rgba(_accent.r, _accent.g, _accent.b, 0.12)
                                            : "transparent"
        // primary(미저장·미전송)는 채움이 아니라 글자색으로 알린다.
        textColor:       !enabled ? qgcPal.buttonText : primary ? _accent : qgcPal.text
    }

    ToolButton {
        objectName: "planToolbar_openButton"
        text: qsTr("Open")
        iconSource: "/qmlimages/Plan.svg"
        enabled: !_planMasterController.syncInProgress
        onClicked: { toolbarButtonClicked(); _openButtonClicked() }
    }

    ToolButton {
        objectName: "planToolbar_saveButton"
        text: qsTr("Save")
        iconSource: "/res/SaveToDisk.svg"
        enabled: !_syncInProgress && _hasPlanItems
        primary: _saveDirty
        onClicked: { toolbarButtonClicked(); _saveButtonClicked() }
    }

    QGCButton {
        id: uploadButton
        objectName: "planToolbar_uploadButton"
        text: qsTr("Upload")
        iconSource: "/res/UploadToVehicle.svg"
        enabled: !_syncInProgress && _hasPlanItems && !_controllerOffline
        visible: !_syncInProgress
        primary: _uploadDirty && !_controllerOffline
        onClicked: { toolbarButtonClicked(); _uploadClicked() }
    }

    ToolButton {
        objectName: "planToolbar_clearButton"
        text: qsTr("Clear")
        iconSource: "/res/TrashCan.svg"
        enabled: !_syncInProgress
        onClicked: { toolbarButtonClicked(); _clearClicked() }
    }

    ToolButton {
        objectName: "planToolbar_hamburgerButton"
        iconSource: "qrc:/qmlimages/Hamburger.svg"

        onClicked: {
            let position = Qt.point(width, height / 2)
            // For some strange reason using mainWindow in mapToItem doesn't work, so we use globals.parent instead which also gets us mainWindow
            position = mapToItem(globals.parent, position)
            var dropPanel = hamburgerDropPanelComponent.createObject(mainWindow, { clickRect: Qt.rect(position.x, position.y, 0, 0) })
            dropPanel.open()
        }
    }

    QGCLabel {
        text:    qsTr("Click in map to add rally points")
        visible: root.showRallyPointsHelp
        Layout.alignment: Qt.AlignVCenter
    }

    Component {
        id: hamburgerDropPanelComponent

        DropPanel {
            id: dropPanel

            sourceComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelHeight / 2

                    QGCButton {
                        objectName: "planToolbar_saveAsButton"
                        Layout.fillWidth: true
                        text: qsTr("Save as...")
                        enabled: !_syncInProgress && _hasPlanItems

                        onClicked: {
                            dropPanel.close()
                            _planMasterController.saveToSelectedFile()
                        }
                    }

                    QGCButton {
                        Layout.fillWidth: true
                        text: qsTr("Save as KML")
                        enabled: !_syncInProgress && _hasPlanItems

                        onClicked: {
                            dropPanel.close()
                            _saveAsKMLClicked()
                        }
                    }

                    QGCButton {
                        Layout.fillWidth: true
                        text: qsTr("Download")
                        enabled: !_syncInProgress && !_controllerOffline
                        visible: !_syncInProgress

                        onClicked: {
                            dropPanel.close()
                            _downloadClicked()
                        }
                    }
                }
            }
        }
    }
}
