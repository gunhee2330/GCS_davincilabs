import QtQuick
import QtMultimedia

import QGC as App
import QGroundControl.Controls

/// Live person count and head mosaics over the EO video. The detector reports boxes
/// normalised in the decoded frame, so they are mapped through the VideoOutput's contentRect,
/// which also accounts for the crop when the panel fills with PreserveAspectCrop.
Item {
    id: root

    required property VideoOutput videoOutput

    /// Mosaic covers the top of each person box, where the face is at drone altitudes.
    readonly property real headFraction: 0.2
    readonly property int  maxBoxes:     16

    readonly property var  _boxes:   App.PersonDetector.boxes
    readonly property rect _content: videoOutput.contentRect

    /// Results older than this are hidden so a stalled stream does not leave boxes frozen.
    property bool _fresh: false
    /// The badge rises at once but falls only on the settle tick, so a person the detector
    /// misses for a frame does not make the number flicker.
    property int _shownCount: 0

    clip:    true
    visible: enabled && App.PersonDetector.active && _fresh

    Connections {
        target: App.PersonDetector
        function onDetectionsChanged() {
            root._fresh = true
            staleTimer.restart()
            if (App.PersonDetector.count > root._shownCount) {
                root._shownCount = App.PersonDetector.count
            }
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
        running:     root.visible
        onTriggered: root._shownCount = App.PersonDetector.count
    }

    Repeater {
        model: Math.min(root._boxes.length, root.maxBoxes)

        delegate: Item {
            id: person
            required property int index
            readonly property rect box: root._boxes[index]

            x:      root._content.x + box.x * root._content.width
            y:      root._content.y + box.y * root._content.height
            width:  box.width * root._content.width
            height: box.height * root._content.height

            Rectangle {
                anchors.fill: parent
                color:        "transparent"
                border.color: "#b3ffd166"
                border.width: 1
            }

            // The video re-rendered into a texture a twelfth the size of the head and drawn
            // back unsmoothed: a mosaic with no shader code.
            ShaderEffectSource {
                width:       person.width
                height:      person.height * root.headFraction
                sourceItem:  root.videoOutput
                sourceRect:  Qt.rect(person.x, person.y, width, height)
                textureSize: Qt.size(Math.max(1, Math.round(width / 12)), Math.max(1, Math.round(height / 12)))
                smooth:      false
                live:        true
            }
        }
    }

    Rectangle {
        anchors.right:   parent.right
        anchors.top:     parent.top
        anchors.margins: 8
        width:           countText.implicitWidth + 18
        height:          countText.implicitHeight + 10
        radius:          3
        color:           "#c0121b24"

        Text {
            id:               countText
            anchors.centerIn: parent
            color:            "#ffd166"
            font.bold:        true
            font.pixelSize:   ScreenTools.defaultFontPixelHeight * 0.7
            text:             qsTr("인원 %1").arg(root._shownCount)
        }
    }
}
