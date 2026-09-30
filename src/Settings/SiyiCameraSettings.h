#pragma once

#include <QtQmlIntegration/QtQmlIntegration>

#include "SettingsGroup.h"

class SiyiCameraSettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    SiyiCameraSettings(QObject* parent = nullptr);
    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(enabled)
    DEFINE_SETTINGFACT(ipAddress)
    DEFINE_SETTINGFACT(port)
    DEFINE_SETTINGFACT(secondaryRtspUrl)
    DEFINE_SETTINGFACT(aiEnabled)
    DEFINE_SETTINGFACT(aiIpAddress)
    DEFINE_SETTINGFACT(aiPort)
    DEFINE_SETTINGFACT(aiRtspUrl)
    DEFINE_SETTINGFACT(fpvRtspUrl)
    DEFINE_SETTINGFACT(autoRecordMission)
    DEFINE_SETTINGFACT(recordingFileName)
    DEFINE_SETTINGFACT(recordingAutoDeleteDays)
    DEFINE_SETTINGFACT(recordingAdminOnly)
    DEFINE_SETTINGFACT(thermalCorrectionEnabled)
    DEFINE_SETTINGFACT(thermalEmissivity)
    DEFINE_SETTINGFACT(thermalHumidity)
    DEFINE_SETTINGFACT(thermalAmbientTempC)
    DEFINE_SETTINGFACT(thermalReflectedTempC)
    DEFINE_SETTINGFACT(sendFcDataToGimbal)
    DEFINE_SETTINGFACT(fcDataLayout)
    DEFINE_SETTINGFACT(crowdModerateCount)
    DEFINE_SETTINGFACT(crowdDenseCount)
    DEFINE_SETTINGFACT(rcKeyFpvChannel)
    DEFINE_SETTINGFACT(rcKeyZoomChannel)
    DEFINE_SETTINGFACT(rcKeyWideChannel)
    DEFINE_SETTINGFACT(rcKeyThermalChannel)
};
