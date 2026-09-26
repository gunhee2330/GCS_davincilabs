import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactControls
import QGroundControl.Controls

Item {
    id: root

    default property alias contentItem: mainLayout.data
    property int sectionFilter: -1

    // Mockup panel padding across, 2.2 cqw
    property real _margins: ScreenTools.mockupUnit * 2.2

    QGCFlickable {
        objectName:     "settingsPageFlickable"
        anchors.fill:   parent
        contentWidth:   mainLayout.width + (root._margins * 2)
        contentHeight:  mainLayout.y + mainLayout.height

        ColumnLayout {
            id:         mainLayout
            // Cards span the panel rather than huddling in its middle, the way the vehicle
            // config pages lay out. Stock centred the content for a full window, which left
            // it adrift of the left-aligned page title and the settings rail beside it
            x:          root._margins
            // A pixel down, or the clip edge takes a first card's top hairline
            y:          1
            width:      Math.max(root.width - (root._margins * 2), implicitWidth, ScreenTools.defaultFontPixelWidth * 50)
            // Card to the next caption, 1.2 cqw in the mockup
            spacing:    ScreenTools.mockupUnit * 1.2
        }
    }
}
