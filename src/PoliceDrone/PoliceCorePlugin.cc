#include "PoliceCorePlugin.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QLatin1String>

#include "AppSettings.h"
#include "FactMetaData.h"

Q_APPLICATION_STATIC(PoliceCorePlugin, _policeCorePluginInstance);

namespace {

struct ColorOverride
{
    const char *name;
    const char *lightEnabled;  ///< nullptr keeps the stock light colours
    const char *darkDisabled;
    const char *darkEnabled;
};

/// Values from the police settings mockup. The window is the police top bar's #0c1218, which the
/// settings rails share; the page beside a rail is settingsPanel, a card on it is card edged in
/// cardBorder, and its descriptions and captions are secondaryText. A selected rail row is
/// selectedRow with a buttonHighlight stripe, a switch or slider groove is controlTrack, and the
/// stepper's end buttons are stepperFill. cardBorder is the mockup's own line: groupBorder,
/// which plan view and the fly panels draw with, keeps its 3:1 value.
/// Disabled variants sit a step dimmer. Status colours and map colours are left alone.
constexpr ColorOverride kOverrides[] = {
    //  name                   light      dark disabled  dark enabled
    { "window",               nullptr,   "#121920",     "#0c1218"   },
    { "windowTransparent",    nullptr,   "#cc121920",   "#cc0c1218" },
    { "windowShadeLight",     nullptr,   "#172029",     "#172029"   },
    { "windowShade",          nullptr,   "#10171f",     "#10171f"   },
    { "windowShadeDark",      nullptr,   "#0a0f14",     "#0a0f14"   },
    { "text",                 nullptr,   "#7d8a99",     "#e8eef4"   },
    { "button",               nullptr,   "#151c25",     "#1a2430"   },
    { "buttonText",           nullptr,   "#7d8a99",     "#e8eef4"   },
    { "buttonHighlight",      "#2f6fd1", "#27405f",     "#4a8ce8"   },
    // Dark text on the accent: white on #4a8ce8 is 3.4:1, #0c1218 is 5.6:1
    { "buttonHighlightText",  nullptr,   "#9fb0c3",     "#0c1218"   },
    // Stock paints primary buttons in the same sky blue as the highlight
    { "primaryButton",        "#2f6fd1", "#27405f",     "#4a8ce8"   },
    { "primaryButtonText",    nullptr,   "#9fb0c3",     "#0c1218"   },
    { "textField",            nullptr,   "#121920",     "#0c1218"   },
    { "textFieldText",        nullptr,   "#7d8a99",     "#e8eef4"   },
    { "card",                 nullptr,   "#131a22",     "#151d26"   },
    { "cardBorder",           nullptr,   "#1e2835",     "#243040"   },
    { "secondaryText",        nullptr,   "#5f6b78",     "#8d9aa8"   },
    { "controlTrack",         nullptr,   "#222c38",     "#2b3745"   },
    { "selectedRow",          nullptr,   "#111c30",     "#13213a"   },
    { "settingsPanel",        nullptr,   "#0f151c",     "#0f151c"   },
    { "stepperFill",          nullptr,   "#1c2733",     "#1c2733"   },
};

}  // namespace

PoliceCorePlugin::PoliceCorePlugin(QObject *parent)
    : QGCCorePlugin(parent)
{
}

QGCCorePlugin *PoliceCorePlugin::instance()
{
    return _policeCorePluginInstance();
}

void PoliceCorePlugin::adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData, bool &userVisible)
{
    QGCCorePlugin::adjustSettingMetaData(settingsGroup, metaData, userVisible);

    // Stock makes light the default on phones and tablets. Only the default moves: SettingsFact
    // falls back to it while nothing is saved, so a scheme picked in Settings still wins.
    if ((settingsGroup == AppSettings::settingsGroup) && (metaData.name() == AppSettings::indoorPaletteName)) {
        metaData.setRawDefaultValue(1);
    }
}

void PoliceCorePlugin::paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo)
{
    for (const ColorOverride &entry : kOverrides) {
        if (colorName != QLatin1String(entry.name)) {
            continue;
        }
        if (entry.lightEnabled) {
            colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled] = QColor(entry.lightEnabled);
        }
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled] = QColor(entry.darkDisabled);
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled] = QColor(entry.darkEnabled);
        return;
    }
}
