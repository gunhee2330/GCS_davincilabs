import QtQuick
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// Detection summary card: the on-device detector's person and vehicle counts with its on/off
/// switch, and the pod AI module's tracking state, so the operator reads all of it in one place
/// instead of off the video.
Item {
    id: root

    readonly property color personColor:  "#ffd166"
    readonly property color vehicleColor: "#4cc9f0"
    readonly property color accentColor:  "#33c7ff"
    readonly property color idleColor:    "#8a9199"

    readonly property real _em: ScreenTools.defaultFontPixelHeight

    /// Results older than the stale timer count as gone, so a stalled stream does not leave the
    /// numbers frozen at their last value.
    property bool _fresh: false
    readonly property bool _live: App.PersonDetector.active && _fresh

    /// Numbers rise at once but fall only on the settle tick, so a person the detector misses
    /// for a frame does not make them flicker.
    property int _persons:  0
    property int _vehicles: 0

    implicitWidth:  ScreenTools.defaultFontPixelWidth * 30
    implicitHeight: column.implicitHeight + _em * 1.2

    Connections {
        target: App.PersonDetector
        function onDetectionsChanged() {
            root._fresh = true
            staleTimer.restart()
            root._persons  = Math.max(root._persons,  App.PersonDetector.count)
            root._vehicles = Math.max(root._vehicles, App.PersonDetector.vehicleCount)
        }
        // Switching off clears the marks; without this the old maximum would show for a tick
        // when switched back on.
        function onActiveChanged() {
            root._persons  = 0
            root._vehicles = 0
        }
    }

    Timer {
        id:          staleTimer
        interval:    1500
        onTriggered: root._fresh = false
    }

    Timer {
        interval:    1000
        repeat:      true
        running:     root._live
        onTriggered: {
            root._persons  = App.PersonDetector.count
            root._vehicles = App.PersonDetector.vehicleCount
        }
    }

    Rectangle {
        anchors.fill: parent
        radius:       6
        color:        "#e5121b24"

        // Keeps a tap on the card from reaching the map underneath, as the camera windows do.
        MouseArea { anchors.fill: parent }
    }

    component Tile: Rectangle {
        id: tile

        property string label
        property string value
        property color  valueColor: "white"
        /// A status word rather than a count: set smaller so it fits the same tile.
        property bool   word: false

        Layout.fillWidth:       true
        Layout.preferredHeight: root._em * 3.3
        radius:                 4
        color:                  "#1a2a36"

        Column {
            anchors.centerIn: parent
            spacing:          2

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color:                    root.idleColor
                font.pixelSize:           Math.max(11, root._em * 0.6)
                text:                     tile.label
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color:                    tile.valueColor
                font.bold:                true
                font.pixelSize:           tile.word ? Math.max(14, root._em * 0.95) : root._em * 1.8
                text:                     tile.value
            }
        }
    }

    ColumnLayout {
        id:              column
        anchors.fill:    parent
        anchors.margins: root._em * 0.6
        spacing:         root._em * 0.4

        RowLayout {
            Layout.fillWidth: true
            spacing:          root._em * 0.4

            Text {
                color:          root.accentColor
                font.bold:      true
                font.pixelSize: Math.max(12, root._em * 0.72)
                text:           qsTr("AI 분석")
            }

            Text {
                Layout.fillWidth: true
                color:            root.idleColor
                font.pixelSize:   Math.max(11, root._em * 0.6)
                text:             root._live ? qsTr("%1 ms").arg(App.PersonDetector.inferenceMs) : ""
            }

            QGCCheckBoxSlider {
                Layout.preferredHeight: ScreenTools.minTouchPixels
                checked:                App.PersonDetector.enabled
                onClicked:              App.PersonDetector.enabled = checked
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing:          root._em * 0.4

            Tile {
                label:      qsTr("인원")
                value:      root._live ? root._persons : "–"
                valueColor: root.personColor
            }

            Tile {
                label:      qsTr("차량")
                value:      root._live ? root._vehicles : "–"
                valueColor: root.vehicleColor
            }

            // The pod's own AI module, driven from the long press on the zoom window.
            Tile {
                readonly property bool _moduleOn: QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue
                readonly property bool _tracking: App.SiyiAiController.hasTarget && !App.SiyiAiController.targetLost

                label: qsTr("추적")
                word:  true
                value: {
                    if (!_moduleOn) {
                        return qsTr("꺼짐")
                    }
                    if (!App.SiyiAiController.connected) {
                        return qsTr("미연결")
                    }
                    if (!App.SiyiAiController.recognitionEnabled) {
                        return qsTr("대기")
                    }
                    if (App.SiyiAiController.hasTarget) {
                        return App.SiyiAiController.targetLost ? qsTr("유실") : qsTr("추적중")
                    }
                    return qsTr("준비")
                }
                valueColor: !_moduleOn ? root.idleColor
                            : !App.SiyiAiController.connected ? "#ff9c46"
                            : _tracking ? "#42d66b"
                            : (App.SiyiAiController.hasTarget ? "#ff5b5b" : "white")
            }
        }
    }
}
