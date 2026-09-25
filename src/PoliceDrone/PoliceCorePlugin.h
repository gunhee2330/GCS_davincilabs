#pragma once

#include "QGCCorePlugin.h"

/// \brief The fork's core plugin. It changes how the app looks and nothing else.
///
/// custom/CMakeLists.txt names this class to QGC's custom-build hook, so
/// QGCCorePlugin::instance() returns it. Every page, row and label stays stock; what changes is
/// the palette (dark surfaces matching the police top bar, our blue in place of the stock sky
/// blue) and the default scheme, which is dark on every platform rather than light on tablets.
class PoliceCorePlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit PoliceCorePlugin(QObject *parent = nullptr);

    static QGCCorePlugin *instance();

    void adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData, bool &userVisible) final;
    void paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo) final;
};
