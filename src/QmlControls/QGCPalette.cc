#include "QGCPalette.h"
#include "QGCCorePlugin.h"

#include <QtCore/QDebug>

QList<QGCPalette*>   QGCPalette::_paletteObjects;

QGCPalette::Theme QGCPalette::_theme = QGCPalette::Dark;

QMap<int, QMap<int, QMap<QString, QColor>>> QGCPalette::_colorInfoMap;

QStringList QGCPalette::_colors;

QGCPalette::QGCPalette(QObject* parent) :
    QObject(parent),
    _colorGroupEnabled(true)
{
    if (_colorInfoMap.isEmpty()) {
        _buildMap();
    }

    // We have to keep track of all QGCPalette objects in the system so we can signal theme change to all of them
    _paletteObjects += this;
}

QGCPalette::~QGCPalette()
{
    bool fSuccess = _paletteObjects.removeOne(this);
    if (!fSuccess) {
        qWarning() << "Internal error";
    }
}

void QGCPalette::_buildMap()
{
    // Structure now rides on the border tokens (field, card, rail divider, check and radio ring),
    // so buttonBorder/groupBorder clear 3:1 on every dark surface rather than copying the mockup
    // line value, which sat at 1.3:1 on the card. Both themes read the same way: the card is a
    // step brighter than the page, the field drops back to page level, and windowShadeLight is a
    // step further from the page than windowShade in whichever direction that theme calls bright.
    //                                      Light                 Dark
    //                                      Disabled   Enabled    Disabled   Enabled
    DECLARE_QGC_COLOR(window,               "#f7f9fb", "#f7f9fb", "#14171b", "#0d0f12")
    DECLARE_QGC_COLOR(windowTransparent,    "#ccf7f9fb", "#ccf7f9fb", "#cc14171b", "#cc0d0f12")
    DECLARE_QGC_COLOR(windowShadeLight,     "#aab5c2", "#aab5c2", "#1c242f", "#1c242f")
    DECLARE_QGC_COLOR(windowShade,          "#e3e8ee", "#e3e8ee", "#141922", "#141922")
    DECLARE_QGC_COLOR(windowShadeDark,      "#ccd4dd", "#ccd4dd", "#10151c", "#10151c")
    DECLARE_QGC_COLOR(text,                 "#5a6675", "#1b2129", "#8896a8", "#eef2f7")
    DECLARE_QGC_COLOR(warningText,          "#cc0808", "#cc0808", "#f85761", "#f85761")
    DECLARE_QGC_COLOR(button,               "#ffffff", "#ffffff", "#212429", "#161c25")
    DECLARE_QGC_COLOR(buttonBorder,         "#93a0af", "#6b7889", "#45505e", "#647285")
    DECLARE_QGC_COLOR(buttonText,           "#5a6675", "#1b2129", "#8896a8", "#eef2f7")
    DECLARE_QGC_COLOR(buttonHighlight,      "#d3dae2", "#1d5fa0", "#2d3a4a", "#7fb2e8")
    DECLARE_QGC_COLOR(buttonHighlightText,  "#5a6675", "#ffffff", "#b6c1cd", "#0d0f12")
    DECLARE_QGC_COLOR(primaryButton,        "#d3dae2", "#1d5fa0", "#2d3a4a", "#7fb2e8")
    DECLARE_QGC_COLOR(primaryButtonText,    "#5a6675", "#ffffff", "#b6c1cd", "#0d0f12")
    DECLARE_QGC_COLOR(textField,            "#f7f9fb", "#f7f9fb", "#212429", "#0d0f12")
    DECLARE_QGC_COLOR(textFieldText,        "#5a6675", "#1b2129", "#8896a8", "#eef2f7")
    DECLARE_QGC_COLOR(mapButton,            "#585858", "#333333", "#585858", "#000000")
    DECLARE_QGC_COLOR(mapButtonHighlight,   "#585858", "#be781c", "#585858", "#be781c")
    DECLARE_QGC_COLOR(mapIndicator,         "#585858", "#be781c", "#585858", "#be781c")
    DECLARE_QGC_COLOR(mapIndicatorChild,    "#585858", "#766043", "#585858", "#766043")
    DECLARE_QGC_COLOR(colorGreen,           "#008f2d", "#008f2d", "#00e04b", "#00e04b")
    DECLARE_QGC_COLOR(colorYellow,          "#a2a200", "#a2a200", "#ffff00", "#ffff00")
    DECLARE_QGC_COLOR(colorYellowGreen,     "#799f26", "#799f26", "#9dbe2f", "#9dbe2f")
    DECLARE_QGC_COLOR(colorOrange,          "#bf7539", "#bf7539", "#de8500", "#de8500")
    DECLARE_QGC_COLOR(colorRed,             "#b52b2b", "#b52b2b", "#f32836", "#f32836")
    DECLARE_QGC_COLOR(colorGrey,            "#808080", "#808080", "#bfbfbf", "#bfbfbf")
    DECLARE_QGC_COLOR(colorBlue,            "#1a72ff", "#1a72ff", "#536dff", "#536dff")
    DECLARE_QGC_COLOR(alertBackground,      "#eecc44", "#eecc44", "#eecc44", "#eecc44")
    DECLARE_QGC_COLOR(alertBorder,          "#808080", "#808080", "#808080", "#808080")
    DECLARE_QGC_COLOR(alertText,            "#000000", "#000000", "#000000", "#000000")
    DECLARE_QGC_COLOR(missionItemEditor,    "#d3dae2", "#e3e8ee", "#161c25", "#1c242f")
    DECLARE_QGC_COLOR(toolStripHoverColor,  "#d3dae2", "#ccd4dd", "#1c242f", "#2d3a4a")
    DECLARE_QGC_COLOR(statusFailedText,     "#9d9d9d", "#000000", "#707070", "#ffffff")
    DECLARE_QGC_COLOR(statusPassedText,     "#9d9d9d", "#000000", "#707070", "#ffffff")
    DECLARE_QGC_COLOR(statusPendingText,    "#9d9d9d", "#000000", "#707070", "#ffffff")
    DECLARE_QGC_COLOR(toolbarBackground,    "#00ffffff", "#00ffffff", "#000d0f12", "#000d0f12")
    DECLARE_QGC_COLOR(groupBorder,          "#93a0af", "#6b7889", "#45505e", "#647285")
    DECLARE_QGC_COLOR(modifiedParamValue,   "#bf7539", "#bf7539", "#de8500", "#de8500")
    // Settings card fill and line, dim text, switch/slider groove, the selected rail row, the page
    // beside the rail and the stepper's end cells. Stock values repeat what those spots drew
    // before they had names of their own
    DECLARE_QGC_COLOR(card,                 "#ffffff", "#ffffff", "#212429", "#161c25")
    DECLARE_QGC_COLOR(cardBorder,           "#93a0af", "#6b7889", "#45505e", "#647285")
    DECLARE_QGC_COLOR(secondaryText,        "#8a95a1", "#5d6772", "#5f6b78", "#96a1ab")
    DECLARE_QGC_COLOR(controlTrack,         "#93a0af", "#6b7889", "#45505e", "#647285")
    DECLARE_QGC_COLOR(selectedRow,          "#d3dae2", "#c9d9f0", "#1c242f", "#1c2a3d")
    DECLARE_QGC_COLOR(settingsPanel,        "#e3e8ee", "#e3e8ee", "#141922", "#141922")
    DECLARE_QGC_COLOR(stepperFill,          "#aab5c2", "#aab5c2", "#1c242f", "#1c242f")

    // Colors not affecting by theming
    //                                                      Disabled     Enabled
    DECLARE_QGC_NONTHEMED_COLOR(brandingPurple,             "#4A2C6D", "#4A2C6D")
    DECLARE_QGC_NONTHEMED_COLOR(brandingBlue,               "#48D6FF", "#6045c5")
    DECLARE_QGC_NONTHEMED_COLOR(toolStripFGColor,           "#707070", "#ffffff")
    DECLARE_QGC_NONTHEMED_COLOR(photoCaptureButtonColor,    "#707070", "#ffffff")
    DECLARE_QGC_NONTHEMED_COLOR(videoCaptureButtonColor,    "#f89a9e", "#f32836")

    // Colors not affecting by theming or enable/disable
    DECLARE_QGC_SINGLE_COLOR(mapWidgetBorderLight,          "#ffffff")
    DECLARE_QGC_SINGLE_COLOR(mapWidgetBorderDark,           "#000000")
    DECLARE_QGC_SINGLE_COLOR(mapMissionTrajectory,          "#be781c")
    DECLARE_QGC_SINGLE_COLOR(surveyPolygonInterior,         "green")
    DECLARE_QGC_SINGLE_COLOR(surveyPolygonTerrainCollision, "red")

}

void QGCPalette::setColorGroupEnabled(bool enabled)
{
    _colorGroupEnabled = enabled;
    emit paletteChanged();
}

void QGCPalette::setGlobalTheme(Theme newTheme)
{
    // Mobile build does not have themes
    if (_theme != newTheme) {
        _theme = newTheme;
        _signalPaletteChangeToAll();
    }
}

void QGCPalette::_signalPaletteChangeToAll()
{
    // Notify all objects of the new theme
    for (QGCPalette *palette : std::as_const(_paletteObjects)) {
        palette->_signalPaletteChanged();
    }
}

void QGCPalette::_signalPaletteChanged()
{
    emit paletteChanged();
}
