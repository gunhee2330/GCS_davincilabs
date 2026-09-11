import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:             _summaryRoot
    anchors.fill:   parent
    anchors.rightMargin: ScreenTools.defaultFontPixelWidth
    anchors.leftMargin:  ScreenTools.defaultFontPixelWidth
    color:          qgcPal.window

    property real _minSummaryW:     ScreenTools.isTinyScreen ? ScreenTools.defaultFontPixelWidth * 28 : ScreenTools.defaultFontPixelHeight * 13.2
    property real _summaryBoxSpace: ScreenTools.defaultFontPixelWidth * 2
    property real _margins:        ScreenTools.defaultFontPixelHeight / 2

    property bool _anyComponentVisible: {
        void QGroundControl.corePlugin.showAdvancedUI // re-bind when maintenance mode toggles
        var vehicle = QGroundControl.multiVehicleManager.activeVehicle
        if (!vehicle) return false
        var components = vehicle.autopilotPlugin.vehicleComponents
        for (var i = 0; i < components.length; i++) {
            if (components[i].summaryQmlSource.toString() !== "" && vehicleConfigView._componentAllowed(components[i])) return true
        }
        return false
    }

    function capitalizeWords(sentence) {
        return sentence.replace(/(?:^|\s)\S/g, function(a) { return a.toUpperCase(); });
    }

    QGCPalette {
        id:                 qgcPal
        colorGroupEnabled:  enabled
    }

    QGCFlickable {
        clip:               true
        anchors.fill:       parent
        contentHeight:      summaryColumn.height
        contentWidth:       _summaryRoot.width
        flickableDirection: Flickable.VerticalFlick

        Column {
            id:             summaryColumn
            width:          _summaryRoot.width
            spacing:        ScreenTools.defaultFontPixelHeight

            QGCLabel {
                width:			parent.width
                wrapMode:		Text.WordWrap
                color:			qgcPal.warningText
                font.bold:      true
                horizontalAlignment: Text.AlignHCenter
                // The complete-state text only narrated the card grid sitting right under it;
                // the unfinished-setup warning is the half that says something the grid does not
                visible:        !setupComplete
                text:           qsTr("WARNING: Configuration tasks remain before this vehicle is ready to fly. Open the red-marked components on the left to finish setup.")

                property bool setupComplete: QGroundControl.multiVehicleManager.activeVehicle ? QGroundControl.multiVehicleManager.activeVehicle.autopilotPlugin.setupComplete : false
            }

            QGCLabel {
                width:               parent.width
                wrapMode:            Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                visible:             !_anyComponentVisible
                text:                qsTr("No configuration items are available for this vehicle.")
            }

            GridLayout {
                id:             _gridCtl
                width:          _summaryRoot.width
                columns:        Math.max(1, Math.floor((_summaryRoot.width + _summaryBoxSpace) / (_minSummaryW + _summaryBoxSpace)))
                columnSpacing:  _summaryBoxSpace
                rowSpacing:     ScreenTools.defaultFontPixelHeight

                Repeater {
                    model: QGroundControl.multiVehicleManager.activeVehicle ? QGroundControl.multiVehicleManager.activeVehicle.autopilotPlugin.vehicleComponents : undefined

                    // Outer summary item rectangle
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        implicitWidth: _minSummaryW
                        implicitHeight: mainLayout.implicitHeight + (_margins * 2)
                        radius: ScreenTools.defaultFontPixelHeight / 2
                        color: qgcPal.button
                        visible: {
                            void QGroundControl.corePlugin.showAdvancedUI // re-bind when maintenance mode toggles
                            return modelData.summaryQmlSource.toString() !== "" && vehicleConfigView._componentAllowed(modelData)
                        }
                        border.width: 1
                        // A card still owing setup is outlined entirely, so it is findable in a grid
                        // of a dozen without reading a single label
                        // At full strength: half alpha composited fainter than the plain border it outranks
                        border.color: setupIncomplete ? qgcPal.colorOrange : qgcPal.groupBorder

                        readonly property real titleHeight: ScreenTools.defaultFontPixelHeight * 2.4
                        readonly property bool showsSetupState: modelData.requiresSetup && modelData.setupSource !== ""
                        readonly property bool setupIncomplete: showsSetupState && !modelData.setupComplete

                        ColumnLayout {
                            id: mainLayout
                            anchors.margins: _margins
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: ScreenTools.defaultFontPixelHeight / 2

                            // Title bar
                            QGCButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: titleHeight
                                text: capitalizeWords(modelData.name)
                                backgroundColor: qgcPal.windowShadeLight
                                showBorder: true
                                backRadius: ScreenTools.defaultFontPixelHeight * 0.39

                                // The stock content item centres the label and has no room for a
                                // chevron; the title is a navigation target, so it reads left to
                                // right and ends in the affordance that says so
                                contentItem: RowLayout {
                                    spacing: ScreenTools.defaultFontPixelWidth

                                    QGCLabel {
                                        Layout.fillWidth:   true
                                        text:               capitalizeWords(modelData.name)
                                        font.bold:          true
                                        elide:              Text.ElideRight
                                    }

                                    // Setup indicator
                                    Rectangle {
                                        id:      setupIndicator
                                        width:   ScreenTools.defaultFontPixelWidth * 1.5
                                        height:  width
                                        radius:  width / 2
                                        color:   setupIncomplete ? qgcPal.colorOrange : qgcPal.colorGreen
                                        visible: showsSetupState
                                    }

                                    QGCColoredImage {
                                        source:  "/InstrumentValueIcons/cheveron-right.svg"
                                        color:   qgcPal.text
                                        opacity: 0.4
                                        width:   ScreenTools.defaultFontPixelHeight * 0.75
                                        height:  width
                                    }
                                }

                                onClicked : {
                                    if (modelData.setupSource !== "" && vehicleConfigView._componentAllowed(modelData)) {
                                        setupView.showVehicleComponentPanel(modelData)
                                    }
                                }
                            }

                            // Summary Qml
                            Loader {
                                id: summaryLoader
                                Layout.fillWidth: true
                                Layout.preferredWidth: item ? item.implicitWidth : 0
                                Layout.preferredHeight: item ? item.implicitHeight : 0
                                source: modelData.summaryQmlSource

                                property var vehicleComponent: modelData
                            }
                        }
                    }
                }
            }
        }
    }
}
