import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Toolbar

Item {
    objectName:    "flyViewToolBarIndicators"
    implicitWidth: mainLayout.width + _widthMargin

    property var  _activeVehicle:           QGroundControl.multiVehicleManager.activeVehicle
    property real _toolIndicatorMargins:    ScreenTools.defaultFontPixelHeight * 0.66
    property real _widthMargin:             _toolIndicatorMargins * 2

    Row {
        id:                 mainLayout
        anchors.margins:    _toolIndicatorMargins
        anchors.left:       parent.left
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        spacing:            ScreenTools.defaultFontPixelWidth * 1.75

        Repeater {
            id:     appRepeater
            model:  QGroundControl.corePlugin.toolBarIndicators
            Loader {
                anchors.top:        parent.top
                anchors.bottom:     parent.bottom
                source:             modelData
                // item is null until the Loader finishes, which logged an
                // undefined-to-bool warning for every indicator on startup.
                visible:            item ? item.showIndicator : false
            }
        }

        Repeater {
            id:     toolIndicatorsRepeater
            model:  _activeVehicle ? _activeVehicle.toolIndicators : []

            Loader {
                anchors.top:        parent.top
                anchors.bottom:     parent.bottom
                source:             modelData
                // item is null until the Loader finishes, which logged an
                // undefined-to-bool warning for every indicator on startup.
                visible:            item ? item.showIndicator : false
            }
        }
    }
}
