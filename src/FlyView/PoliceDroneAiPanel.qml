import QtQuick
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// Detection card: person and vehicle counts from the on-device detector and the pod AI
/// module's tracking state. Styled like the telemetry bar it sits beside so the two read as
/// one instrument row. The switch itself lives in the top bar.
Item {
    id: root

    readonly property real _em: ScreenTools.defaultFontPixelHeight

    /// Results older than the stale timer count as gone, so a stalled stream does not leave the
    /// numbers frozen at their last value.
    property bool _fresh: false
    readonly property bool _live: App.PersonDetector.active && _fresh

    /// Numbers rise at once but fall only on the settle tick, so a person the detector misses
    /// for a frame does not make them flicker.
    property int _persons:  0
    property int _vehicles: 0

    readonly property bool _moduleOn:  QGroundControl.settingsManager.siyiCameraSettings.aiEnabled.rawValue
    readonly property bool _tracking:  App.SiyiAiController.hasTarget && !App.SiyiAiController.targetLost

    /// One band for every value, so a Korean status word and a digit sit on the same line
    /// however their fonts measure.
    readonly property real _valueHeight: Math.max(18, _em * 1.25)

    implicitWidth:  row.implicitWidth + _em
    implicitHeight: Math.max(ScreenTools.minTouchPixels, _em * 2.9)

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

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
        id: staleTimer
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

    // Same surface as TelemetryValuesBar.
    Rectangle {
        anchors.fill: parent
        color:        qgcPal.window
        radius:       ScreenTools.defaultFontPixelWidth / 2
        opacity:      0.75
        // Keeps a tap on the strip from reaching the map underneath, as the camera windows do.
        MouseArea { anchors.fill: parent }
    }

    component Stat: Column {
        id: stat
        property string label
        property string value
        property color  dot
        /// A status word rather than a count: set smaller so it still fits the value band.
        property bool   word: false

        spacing: root._em * 0.15

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing:                  root._em * 0.28

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width:                  root._em * 0.36
                height:                 root._em * 0.36
                radius:                 width / 2
                color:                  stat.dot
            }

            Text {
                color:          qgcPal.text
                opacity:        0.7
                font.pixelSize: Math.max(12, root._em * 0.62)
                text:           stat.label
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            height:                   root._valueHeight
            verticalAlignment:        Text.AlignVCenter
            color:                    qgcPal.text
            font.family:              stat.word ? ScreenTools.normalFontFamily : "Open Sans"
            font.weight:              Font.DemiBold
            font.pixelSize:           stat.word ? Math.max(13, root._em * 0.82)
                                                : Math.max(16, root._em * 1.15)
            text:                     stat.value
        }
    }

    component Divider: Rectangle {
        Layout.alignment:       Qt.AlignVCenter
        Layout.preferredWidth:  1
        Layout.preferredHeight: root._em * 1.8
        color:                  qgcPal.text
        opacity:                0.15
    }

    RowLayout {
        id:               row
        anchors.centerIn: parent
        spacing:          root._em * 0.55

        Stat { label: qsTr("인원"); value: root._live ? root._persons  : "–"; dot: "#e0a800" }

        Divider {}

        Stat { label: qsTr("차량"); value: root._live ? root._vehicles : "–"; dot: "#1f9fd0" }

        Divider {}

        // The pod's own AI module, driven from the long press on the zoom window.
        Stat {
            label: qsTr("추적")
            word:  true
            value: {
                if (!root._moduleOn)                            return qsTr("꺼짐")
                if (!App.SiyiAiController.connected)            return qsTr("미연결")
                if (!App.SiyiAiController.recognitionEnabled)   return qsTr("대기")
                if (App.SiyiAiController.hasTarget)
                    return App.SiyiAiController.targetLost ? qsTr("유실") : qsTr("추적중")
                return qsTr("준비")
            }
            dot: !root._moduleOn ? "#9aa3ab"
                 : !App.SiyiAiController.connected ? "#ff9c46"
                 : root._tracking ? "#42d66b"
                 : (App.SiyiAiController.hasTarget ? "#ff5b5b" : "#1f9fd0")
        }

    }
}
