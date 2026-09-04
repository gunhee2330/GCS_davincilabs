import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.PlanView
import QGroundControl.FactControls

Rectangle {
    id: root
    height: _currentItem ? valuesRect.y + valuesRect.height + _innerMargin : titleLayout.y + titleLayout.height + _margin
    // 목록 선택 표시는 파란 채움이 아니라 옅은 틴트다. 항목 편집기와 같은 방식.
    color: _currentItem ? Qt.rgba(PolicePalette.accentDark.r, PolicePalette.accentDark.g,
                                  PolicePalette.accentDark.b, 0.14)
                        : qgcPal.window
    radius: _radius

    property var rallyPoint ///< RallyPoint object associated with editor
    property var controller ///< RallyPointController

    property bool _currentItem: rallyPoint ? rallyPoint === controller.currentRallyPoint : false
    property color _outerTextColor: qgcPal.text

    readonly property real _margin: ScreenTools.defaultFontPixelWidth / 2
    readonly property real  _innerMargin: 2
    readonly property real _radius: ScreenTools.defaultFontPixelWidth / 2
    readonly property real _titleHeight: ScreenTools.implicitComboBoxHeight + ScreenTools.defaultFontPixelWidth

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    RowLayout {
        id: titleLayout
        anchors.margins: _margin
        anchors.left: parent.left
        anchors.rightMargin: _margin * 2
        anchors.right: parent.right
        height: _titleHeight
        spacing: ScreenTools.defaultFontPixelWidth

        QGCLabel {
            text: qsTr("Rally Point")
            color: _outerTextColor
        }

        QGCColoredImage {
            id: deleteButton
            Layout.alignment: Qt.AlignRight
            Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 0.5
            Layout.preferredHeight: Layout.preferredWidth
            source: "/res/XDelete.svg"
            color: _outerTextColor
        }
    }

    QGCMouseArea {
        id: selectMouseArea
        anchors.top: titleLayout.top
        anchors.bottomMargin: -_margin
        anchors.bottom: titleLayout.bottom
        anchors.left: titleLayout.left
        anchors.right: titleLayout.right
        onClicked: {
            if (mainWindow.allowViewSwitch()) {
                controller.currentRallyPoint = rallyPoint
            }
        }
    }

    QGCMouseArea {
        anchors.top: selectMouseArea.top
        anchors.bottom: selectMouseArea.bottom
        anchors.right: selectMouseArea.right
        width: height
        onClicked: {
            if (mainWindow.allowViewSwitch()) {
                controller.removePoint(rallyPoint)
            }
        }
    }

    Rectangle {
        id: valuesRect
        anchors.margins: _innerMargin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleLayout.bottom
        height: valuesLayout.height + (_margin * 2)
        // 편집기 배경을 패널 바닥과 한 톤으로 둔다. windowShadeDark 를 쓰면 상자 안의 상자가 되고,
        // 그 색을 팔레트에서 바꾸면 분석 화면·설정 화면까지 따라 바뀐다.
        color: qgcPal.window
        visible: _currentItem
        radius: _radius

        ColumnLayout {
            id: valuesLayout
            anchors.margins: _margin
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: _margin

            Repeater {
                model: rallyPoint ? rallyPoint.textFieldFacts : 0

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label: modelData.shortDescription
                    fact: modelData
                }
            }
        }
    }
}
