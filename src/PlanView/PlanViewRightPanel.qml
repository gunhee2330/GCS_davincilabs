import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.PlanView

Item {
    required property var editorMap
    required property var planMasterController

    signal editingLayerChangeRequested(int layer)

    id: root

    property var  _missionController: planMasterController.missionController
    property real _toolsMargin:       ScreenTools.defaultFontPixelWidth * 0.75

    function selectNextNotReady() {
        for (var i = 0; i < _missionController.visualItems.count; i++) {
            var vmi = _missionController.visualItems.get(i)
            if (vmi.readyForSaveState === VisualMissionItem.NotReadyForSaveData) {
                _missionController.setCurrentPlanViewSeqNum(vmi.sequenceNumber, true)
                break
            }
        }
    }

    QGCPalette { id: qgcPal }

    Rectangle {
        id:             rightPanelBackground
        anchors.fill:   parent
        color:          qgcPal.window
        opacity:        0.85
    }


    // Open/Close panel
    Item {
        id:                     panelOpenCloseButton
        anchors.right:          parent.left
        anchors.verticalCenter: parent.verticalCenter
        width:                  toggleButtonRect.width - toggleButtonRect.radius
        height:                 toggleButtonRect.height
        clip:                   true

        property bool _expanded: root.anchors.right == root.parent.right

        // 7인치에서 1mm = 8.49px. 순정 폭 18px 은 2.1mm 라 손가락으로 잡히지 않는다.
        // 손잡이는 반쯤 잘려 보이므로 폭은 잘린 뒤에도 28px 이상 남게 잡는다.
        Rectangle {
            id:             toggleButtonRect
            width:          Math.max(40, ScreenTools.defaultFontPixelWidth * 4.5)
            height:         Math.max(76, width * 2)
            radius:         ScreenTools.defaultBorderRadius * 2
            color:          rightPanelBackground.color
            opacity:        rightPanelBackground.opacity

            QGCLabel {
                id:                 toggleButtonLabel
                anchors.centerIn:   parent
                text:               panelOpenCloseButton._expanded ? ">" : "<"
                font.pointSize:     ScreenTools.mediumFontPointSize
                color:              qgcPal.text
            }

        }

        QGCMouseArea {
            anchors.fill: parent

            onClicked: {
                if (panelOpenCloseButton._expanded) {
                    // Close panel
                    root.anchors.right = undefined
                    root.anchors.left = root.parent.right
                } else {
                    // Open panel
                    root.anchors.left = undefined
                    root.anchors.right = root.parent.right
                }
            }
        }
    }

    //-------------------------------------------------------
    // Right Panel Controls
    Item {
        anchors.fill: rightPanelBackground

        DeadMouseArea {
            anchors.fill:   parent
        }

        // 임무 전체 숫자. 트리와 함께 스크롤되지 않고 맨 위에 붙어 있다.
        MissionStats {
            id:                     missionSummary
            anchors.top:            parent.top
            anchors.left:           parent.left
            anchors.right:          parent.right
            planMasterController:   root.planMasterController
        }

        PlanTreeView {
            id:                     planTreeView
            objectName:             "planView_planTree"
            anchors.top:            missionSummary.bottom
            anchors.bottom:         parent.bottom
            anchors.left:           parent.left
            anchors.right:          parent.right
            editorMap:              root.editorMap
            planMasterController:   root.planMasterController
            onEditingLayerChangeRequested: (layer) => root.editingLayerChangeRequested(layer)
        }
    }

    function selectLayer(nodeType) {
        // Ensure panel is open
        if (!panelOpenCloseButton._expanded) {
            root.anchors.left = undefined
            root.anchors.right = root.parent.right
        }
        planTreeView.selectLayer(nodeType)
    }
}
