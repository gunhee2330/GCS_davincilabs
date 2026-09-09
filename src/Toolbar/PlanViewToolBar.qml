import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls
import QGroundControl.PlanView

Rectangle {
    id: _root
    width: parent.width
    height: PoliceBar.height
    color: PoliceBar.color

    property var planMasterController
    property bool showRallyPointsHelp: false

    signal toolbarButtonClicked()

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property real _controllerProgressPct: planMasterController.missionController.progressPct

    QGCPalette { id: qgcPal }

    // The Fly view's police dashboard carries its own ☰; without this the button vanishes on
    // switching to Plan, leaving the logo as the only — undiscoverable — way back.
    QGCToolBarButton {
        id: menuButton
        objectName: "toolbar_mainMenu"
        anchors.left: parent.left
        anchors.leftMargin: PoliceBar.margin
        width: PoliceBar.menuButtonWidth
        height: parent.height
        // No padding of its own: the fly view's bar is the reference and its menu glyph is
        // centred in a plain touch-sized button. This component's own horizontal padding is
        // what put the mark on this bar thirteen pixels off the one next door.
        leftPadding: 0
        rightPadding: 0
        icon.source: "qrc:/qmlimages/Hamburger.svg"
        iconHeight: PoliceBar.iconSize
        iconColor: PoliceBar.content
        onClicked: mainWindow.showToolSelectDialog()
    }

    QGCToolBarButton {
        id: qgcButton
        objectName: "toolbar_qgcLogo"
        anchors.left: menuButton.right
        anchors.leftMargin: PoliceBar.margin
        height: parent.height
        leftPadding: 0
        rightPadding: 0
        icon.source: "/res/DavinciLabsLogo.png"
        iconAspectRatio: 1153 / 122
        iconHeight: PoliceBar.logoHeight
        iconColor: PoliceBar.content
        onClicked: mainWindow.showToolSelectDialog()
    }

    QGCFlickable {
        id: toolsFlickable
        anchors.bottomMargin: 1
        anchors.left: qgcButton.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        contentWidth: toolIndicators.width
        flickableDirection: Flickable.HorizontalFlick

        PlanToolBarIndicators {
            id: toolIndicators
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            planMasterController: _root.planMasterController
            showRallyPointsHelp: _root.showRallyPointsHelp
            barContent: PoliceBar.content
            onToolbarButtonClicked: _root.toolbarButtonClicked()
        }
    }

    // Small mission download progress bar
    Rectangle {
        id: progressBar
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: 4
        width: _controllerProgressPct * parent.width
        color: qgcPal.colorGreen
        visible: false

        onVisibleChanged: {
            if (visible) {
                largeProgressBar._userHide = false
            }
        }
    }

    // Large mission download progress bar
    Rectangle {
        id: largeProgressBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: parent.height
        color: qgcPal.window
        visible: _showLargeProgress

        property bool _userHide: false
        property bool _showLargeProgress: progressBar.visible && !_userHide && qgcPal.globalTheme === QGCPalette.Light

        Connections {
            target: QGroundControl.multiVehicleManager
            function onActiveVehicleChanged(activeVehicle) { largeProgressBar._userHide = false }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: _controllerProgressPct * parent.width
            color: qgcPal.colorGreen
        }

        QGCLabel {
            anchors.centerIn: parent
            text: qsTr("Syncing Mission")
            font.pointSize: ScreenTools.largeFontPointSize
            visible: _controllerProgressPct !== 1
        }

        QGCLabel {
            anchors.centerIn: parent
            text: qsTr("Done")
            font.pointSize: ScreenTools.largeFontPointSize
            visible: _controllerProgressPct === 1
        }

        QGCLabel {
            anchors.margins: _margin
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            text: qsTr("Click anywhere to hide")

            property real _margin: ScreenTools.defaultFontPixelWidth / 2
        }

        MouseArea {
            anchors.fill: parent
            onClicked: largeProgressBar._userHide = true
        }
    }

    // Progress bar
    Connections {
        target: planMasterController.missionController

        function onProgressPctChanged(progressPct) {
            if (progressPct === 1) {
                if (_root.visible) {
                    resetProgressTimer.start()
                } else {
                    progressBar.visible = false
                }
            } else if (progressPct > 0) {
                progressBar.visible = true
            }
        }
    }

    Timer {
        id: resetProgressTimer
        interval: 3000
        onTriggered: progressBar.visible = false
    }
}
