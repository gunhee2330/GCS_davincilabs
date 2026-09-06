import QtQuick
import QtQuick.Layouts

import QGC as App
import QGroundControl
import QGroundControl.Controls

/// One-line detection strip: the on-device detector's switch and person and vehicle counts,
/// and the pod AI module's tracking state. Styled like the telemetry bar it sits above so the
/// two read as one instrument.
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

    implicitWidth:  row.implicitWidth + _em * 1.2
    implicitHeight: Math.max(ScreenTools.minTouchPixels, _em * 2)

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

    // Same surface as TelemetryValuesBar.
    Rectangle {
        anchors.fill: parent
        color:        qgcPal.window
        radius:       ScreenTools.defaultFontPixelWidth / 2
        opacity:      0.75
        // Keeps a tap on the strip from reaching the map underneath, as the camera windows do.
        MouseArea { anchors.fill: parent }
    }

    component Stat: RowLayout {
        id: stat
        property string label
        property string value
        property color  dot
        /// A status word rather than a count: set smaller so it sits on the same line.
        property bool   word: false
        spacing: root._em * 0.3
        Rectangle {
            Layout.preferredWidth:  root._em * 0.36
            Layout.preferredHeight: root._em * 0.36
            radius:                 width / 2
            color:                  stat.dot
        }
        Text {
            color:          qgcPal.text
            opacity:        0.7
            font.pixelSize: Math.max(12, root._em * 0.64)
            text:           stat.label
        }
        Text {
            color:          qgcPal.text
            font.family:    stat.word ? ScreenTools.normalFontFamily : "Open Sans"
            font.weight:    stat.word ? Font.Bold : Font.DemiBold
            font.pixelSize: stat.word ? Math.max(12, root._em * 0.68) : Math.max(14, root._em * 0.95)
            text:           stat.value
        }
    }

    RowLayout {
        id:               row
        anchors.centerIn: parent
        height:           parent.height
        spacing:          root._em * 0.55

        QGCCheckBoxSlider {
            Layout.preferredHeight: root.height
            checked:                App.PersonDetector.enabled
            onClicked:              App.PersonDetector.enabled = checked
        }

        Text {
            color:          qgcPal.text
            font.bold:      true
            font.pixelSize: Math.max(12, root._em * 0.64)
            text:           qsTr("AI")
        }

        Rectangle {
            Layout.preferredWidth:  1
            Layout.preferredHeight: root._em
            color:                  qgcPal.text
            opacity:                0.15
        }

        Stat { label: qsTr("인원"); value: root._live ? root._persons  : "–"; dot: "#e0a800" }
        Stat { label: qsTr("차량"); value: root._live ? root._vehicles : "–"; dot: "#1f9fd0" }

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
