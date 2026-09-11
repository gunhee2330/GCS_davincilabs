import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:             _root
    width:          80
    height:         20
    border.width:   1
    // A black border vanishes against the dark theme's surfaces
    border.color:   qgcPal.buttonBorder

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    signal colorSelected(var color)

    ColorDialog {
        id: colorDialog
        onAccepted: {
            _root.colorSelected(colorDialog.selectedColor)
            colorDialog.close()
        }
    }

    MouseArea {
        anchors.fill: parent

        onClicked: {
            colorDialog.selectedColor = _root.color
            colorDialog.open()
        }
    }
}
