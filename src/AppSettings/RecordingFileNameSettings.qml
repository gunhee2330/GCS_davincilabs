import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// 녹화: the name a pod video gets when it is downloaded to the tablet (RFP p9), with the example
/// under the label following the chosen form.
SettingsGroupLayout {
    Layout.fillWidth:   true
    heading:            qsTr("녹화")

    property Fact _fact: QGroundControl.settingsManager.siyiCameraSettings.recordingFileName

    SettingsRow {
        label:          qsTr("녹화 파일 이름")
        description:    qsTr("받을 때 붙는 이름, 예: %1").arg(_fact.rawValue === 1 ? "20260928143015123_37.5665_126.9780.mp4"
                                                                             : "20260928143015123.mp4")

        QGCComboBox {
            objectName:         "settingsComboBox_recordingFileName"
            Layout.fillWidth:   true
            model:              [ qsTr("연월일시분초"), qsTr("연월일시분초_위도_경도") ]
            currentIndex:       _fact.rawValue === 1 ? 1 : 0
            onActivated:        (index) => _fact.rawValue = index
        }
    }
}
