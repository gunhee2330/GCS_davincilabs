import QGroundControl
import QGroundControl.Controls

ToolStripAction {
    text:       qsTr("SIYI")
    iconSource: "/qmlimages/camera_photo.svg"
    visible:    QGroundControl.settingsManager.siyiCameraSettings.userVisible
                && QGroundControl.settingsManager.siyiCameraSettings.enabled.rawValue

    dropPanelComponent: Component {
        SiyiCameraControlPanel {
        }
    }
}
