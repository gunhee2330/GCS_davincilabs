import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.PlanView
import QGroundControl.FactControls

// Editor for Simple mission items
Rectangle {
    required property var missionItem
    required property real availableWidth

    id: root
    width: availableWidth
    height: editorColumn.height + (_margin * 2)
    // 편집기 배경을 패널 바닥과 한 톤으로 둔다. windowShadeDark 를 쓰면 상자 안의 상자가 되고,
    // 그 색을 팔레트에서 바꾸면 분석 화면·설정 화면까지 따라 바뀐다.
    color: qgcPal.window
    // 입력 영역을 경찰 청색 테두리로 묶는다. 배경은 패널과 같은 톤이라 상자가 되지 않는다.
    border.width: PolicePalette.borderWidth
    border.color: PolicePalette.blue
    radius: _radius


    property bool _specifiesAltitude: missionItem.specifiesAltitude
    property real _margin: ScreenTools.defaultFontPixelHeight / 2
    property real _altRectMargin: ScreenTools.defaultFontPixelWidth / 2
    property var _controllerVehicle: missionItem.masterController.controllerVehicle
    property int _globalAltFrame: missionItem.masterController.missionController.globalAltitudeFrame
    property bool _globalAltFrameIsMixed: _globalAltFrame == QGroundControl.AltitudeFrameMixed
    property real _radius: ScreenTools.defaultFontPixelWidth / 2
    property real _fieldSpacing: ScreenTools.defaultFontPixelHeight / 2

    // 7인치 터치(1mm = 8.49px): 입력 필드 한 줄이 기본 30px(3.5mm) 라 손가락으로 정확히 눌리지 않는다.
    // 52px(6.1mm)로 올리되, height 를 직접 주면 컨트롤 안 글자가 위로 붙으므로 상하 여백으로 키운다.
    // 글자높이(defaultFontPixelHeight) + 여백×2 = defaultFontPixelHeight * 3.25 = 52px.
    readonly property real _touchFieldPadding: Math.max(ScreenTools.comboBoxPadding, ScreenTools.defaultFontPixelHeight * 1.125)

    // FactTextFieldSlider 안에서 실제로 눌리는 것은 TextField 하나뿐이라 바깥 사각형만 키우면
    // 보기만 커지고 탭 영역은 그대로다. 공용 컨트롤(QmlControls)을 건드리지 않기 위해
    // 인스턴스마다 노출된 alias 로 그 TextField 의 상하 여백만 늘린다.
    function _growTouchField(fieldSlider) {
        var textField = fieldSlider.textField
        textField.topPadding = Math.max(textField.topPadding, _touchFieldPadding)
        textField.bottomPadding = Math.max(textField.bottomPadding, _touchFieldPadding)
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: root.enabled }

    Column {
        id: editorColumn
        anchors.margins: _margin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: _margin

        // Takeoff item
        ColumnLayout {
            anchors.margins: _margin
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: _margin
            visible: missionItem.isTakeoffItem && missionItem.wizardMode // Hack special case for takeoff item

            QGCLabel {
                text: qsTr("Move '%1' %2 to the %3 location. %4")
                    .arg(_controllerVehicle.vtol ? qsTr("T") : qsTr("T"))
                    .arg(_controllerVehicle.vtol ? qsTr("Transition Direction") : qsTr("Takeoff"))
                    .arg(_controllerVehicle.vtol ? qsTr("desired") : qsTr("climbout"))
                    .arg(_controllerVehicle.vtol ? (qsTr("Ensure distance from launch to transition direction is far enough to complete transition.")) : "")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: !initialClickLabel.visible
            }

            QGCLabel {
                text: qsTr("Ensure clear of obstacles and into the wind.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: !initialClickLabel.visible
            }

            QGCButton {
                text: qsTr("Done")
                Layout.fillWidth: true
                visible: !initialClickLabel.visible
                onClicked: {
                    missionItem.wizardMode = false
                }
            }

            QGCLabel {
                id: initialClickLabel
                text: missionItem.launchTakeoffAtSameLocation ?
                                        qsTr("Click in map to set planned Takeoff location.") :
                                        qsTr("Click in map to set planned Launch location.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: missionItem.isTakeoffItem && !missionItem.launchCoordinate.isValid
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: _fieldSpacing
            visible: !missionItem.wizardMode

            QGCTabBar {
                id: tabBar
                Layout.fillWidth: true
                visible: _multipleTabsVisible()

                property bool showBasicItems:    tabBar.visible ? tabBar.currentIndex === 0 : _basicItemsAvailable
                property bool showCameraItems:   tabBar.visible ? tabBar.currentIndex === 1 : _cameraAvailable
                property bool showAdvancedItems: tabBar.visible ? tabBar.currentIndex === 2 : _advancedItemsAvailable

                property bool _basicItemsAvailable: _specifiesAltitude || missionItem.speedSection.available || missionItem.comboboxFacts.count > 0 || missionItem.textFieldFacts.count > 0 || missionItem.nanFacts.count > 0
                property bool _advancedItemsAvailable: missionItem.comboboxFactsAdvanced.count > 0 || missionItem.textFieldFactsAdvanced.count > 0 || missionItem.nanFactsAdvanced.count > 0
                property bool _cameraAvailable: missionItem.cameraSection.available

                function _multipleTabsVisible() {
                    let visibleCount = 0
                    if (_basicItemsAvailable) visibleCount++
                    if (_cameraAvailable) visibleCount++
                    if (_advancedItemsAvailable) visibleCount++
                    return visibleCount > 1
                }

                Component.onCompleted: {
                    if (_basicItemsAvailable) {
                        tabBar.currentIndex = 0
                    } else if (_cameraAvailable) {
                        tabBar.currentIndex = 1
                    } else if (_advancedItemsAvailable) {
                        tabBar.currentIndex = 2
                    } else {
                        tabBar.currentIndex = -1
                    }
                }

                QGCTabButton {
                    id: basicItemsTab
                    icon.source: "/res/PlanSimpleItemBasic.svg"
                    visible: tabBar._basicItemsAvailable
                }

                QGCTabButton {
                    id: cameraTab
                    icon.source: "/res/PlanSimpleItemCamera.svg"
                    visible: tabBar._cameraAvailable
                }

                QGCTabButton {
                    id: advancedItemsTab
                    icon.source: "/res/PlanSimpleItemAdvanced.svg"
                    visible: tabBar._advancedItemsAvailable
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: _fieldSpacing
                visible: tabBar.showBasicItems

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: _fieldSpacing
                    visible: _specifiesAltitude

                    RowLayout {
                        Layout.fillWidth: true
                        visible: _globalAltFrameIsMixed

                        QGCLabel {
                            Layout.fillWidth: true
                            text: qsTr("Alt Frame")
                        }

                        AltFrameCombo {
                            topPadding: root._touchFieldPadding
                            bottomPadding: root._touchFieldPadding
                            altitudeFrame: missionItem.altitudeFrame
                            vehicle: _controllerVehicle
                            onAltitudeFrameChanged: missionItem.altitudeFrame = altitudeFrame
                        }
                    }

                    FactTextFieldSlider {
                        id: altField
                        Layout.fillWidth: true
                        label: qsTr("Altitude%1").arg(_extraLabelText())
                        fact: missionItem.altitude

                        Component.onCompleted: root._growTouchField(altField)

                        function _extraLabelText() {
                            return qsTr(" (%1)").arg(QGroundControl.altitudeFrameExtraUnits(missionItem.altitudeFrame))
                        }
                    }

                    QGCLabel {
                        font.pointSize: ScreenTools.smallFontPointSize
                        text: qsTr("Actual AMSL alt sent: %1 %2").arg(missionItem.amslAltAboveTerrain.valueString).arg(missionItem.amslAltAboveTerrain.units)
                        visible: missionItem.altitudeFrame === QGroundControl.AltitudeFrameCalcAboveTerrain
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: _fieldSpacing

                    Repeater {
                        model: missionItem.comboboxFacts

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QGCLabel {
                                font.pointSize: ScreenTools.smallFontPointSize
                                text: object.name
                                visible: object.name !== ""
                            }

                            FactComboBox {
                                Layout.fillWidth: true
                                topPadding: root._touchFieldPadding
                                bottomPadding: root._touchFieldPadding
                                indexModel: false
                                model: object.enumStrings
                                fact: object
                            }
                        }
                    }
                }

                Repeater {
                    model: missionItem.textFieldFacts

                    FactTextFieldSlider {
                        id: textFieldFactRow
                        Layout.fillWidth: true
                        label: object.name
                        fact: object
                        enabled: !object.readOnly
                        warnOnUserMinMaxInvalid: false

                        Component.onCompleted: root._growTouchField(textFieldFactRow)
                    }
                }

                Repeater {
                    model: missionItem.nanFacts

                    FactTextFieldSlider {
                        id: nanFactRow
                        Layout.fillWidth: true
                        label: object.name
                        fact: object
                        showEnableCheckbox: true
                        enableCheckBoxChecked: !isNaN(object.rawValue)
                        warnOnUserMinMaxInvalid: false

                        onEnableCheckboxClicked: object.rawValue = enableCheckBoxChecked ? 0 : NaN

                        Component.onCompleted: root._growTouchField(nanFactRow)
                    }
                }

                FactTextFieldSlider {
                    id: flightSpeedRow
                    Layout.fillWidth: true
                    label: qsTr("Flight Speed")
                    fact: missionItem.speedSection.flightSpeed
                    showEnableCheckbox: true
                    enableCheckBoxChecked: missionItem.speedSection.specifyFlightSpeed
                    visible: missionItem.speedSection.available

                    onEnableCheckboxClicked: missionItem.speedSection.specifyFlightSpeed = enableCheckBoxChecked

                    Component.onCompleted: root._growTouchField(flightSpeedRow)
                }
            }

            CameraSection {
                Layout.fillWidth: true
                showSectionHeader: false
                missionItem: root.missionItem
                visible: tabBar.showCameraItems

                Component.onCompleted: checked = missionItem.cameraSection.settingsSpecified
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: _fieldSpacing
                visible: tabBar.showAdvancedItems

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: _fieldSpacing

                    Repeater {
                        model: missionItem.comboboxFactsAdvanced

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QGCLabel {
                                font.pointSize: ScreenTools.smallFontPointSize
                                text: object.name
                                visible: object.name !== ""
                            }

                            FactComboBox {
                                Layout.fillWidth: true
                                topPadding: root._touchFieldPadding
                                bottomPadding: root._touchFieldPadding
                                indexModel: false
                                model: object.enumStrings
                                fact: object
                            }
                        }
                    }
                }

                Repeater {
                    model: missionItem.textFieldFactsAdvanced

                    FactTextFieldSlider {
                        id: advancedTextFieldFactRow
                        Layout.fillWidth: true
                        label: object.name
                        fact: object
                        enabled: !object.readOnly
                        warnOnUserMinMaxInvalid: false

                        Component.onCompleted: root._growTouchField(advancedTextFieldFactRow)
                    }
                }

                Repeater {
                    model: missionItem.nanFactsAdvanced

                    FactTextFieldSlider {
                        id: advancedNanFactRow
                        Layout.fillWidth: true
                        label: object.name
                        fact: object
                        showEnableCheckbox: true
                        enableCheckBoxChecked: !isNaN(object.rawValue)
                        warnOnUserMinMaxInvalid: false

                        onEnableCheckboxClicked: object.rawValue = enableCheckBoxChecked ? 0 : NaN

                        Component.onCompleted: root._growTouchField(advancedNanFactRow)
                    }
                }
            }
        }
    }
}
