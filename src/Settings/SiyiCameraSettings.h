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
};
