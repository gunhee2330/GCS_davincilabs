import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Collapsible section heading for the plan editors.
///
/// Drop-in for the stock SectionHeader, which sets the title in body text and rules a full width
/// line under it at full text brightness. Twenty four of those stacked down a 240px panel are the
/// single loudest thing about the stock look, and the line carries no information the spacing does
/// not already carry. This keeps the same checkable behaviour and the same properties, and spends
/// a small dim label plus whitespace instead.
CheckBox {
    id:             control
    focusPolicy:    Qt.ClickFocus
    checked:        true
    leftPadding:    0

    property var            color:          qgcPal.text
    property bool           showSpacer:     true
    property ButtonGroup    buttonGroup:    null

    onButtonGroupChanged: {
        if (buttonGroup) {
            buttonGroup.addButton(control)
        }
    }

    QGCPalette { id: qgcPal }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 0.9
            width:                  1
            visible:                control.showSpacer
        }

        RowLayout {
            Layout.fillWidth: true
            spacing:          ScreenTools.defaultFontPixelWidth * 0.6

            QGCLabel {
                text:           control.text
                color:          control.color
                opacity:        0.65
                font.pointSize: ScreenTools.smallFontPointSize
                font.bold:      true
            }

            // 접힘을 알리는 것은 화살표 하나면 된다. 제목 오른쪽 끝까지 선을 그을 이유가 없다.
            QGCColoredImage {
                Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.55
                Layout.preferredHeight: Layout.preferredWidth
                source:                 "/qmlimages/arrow-down.png"
                color:                  control.color
                opacity:                0.65
                fillMode:               Image.PreserveAspectFit
                visible:                !control.checked
            }

            Item { Layout.fillWidth: true }
        }

        Item {
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 0.25
            width:                  1
        }
    }

    indicator: Item {}
}
