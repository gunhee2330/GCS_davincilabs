import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Standard push button control:
///     If there is both an icon and text the icon will be to the left of the text
///     If icon only, icon will be centered
Button {
    property bool primary: false
    // The dark button fill is the same value as the window and the card it sits on, so the
    // border is the only thing that draws the button - keep it in both themes
    property bool showBorder: true
    property real backRadius: _settingsLook ? ScreenTools.mockupUnit * 0.4 : ScreenTools.defaultBorderRadius
    property real heightFactor: 0.5
    property string iconSource: ""
    property real fontWeight: Font.Normal // default for qml Text
    property real pointSize: _settingsLook ? ScreenTools.mockupPointUnit * 1.05 : ScreenTools.defaultFontPointSize

    property alias wrapMode: text.wrapMode
    property alias horizontalAlignment: text.horizontalAlignment
    property alias backgroundColor: backRect.color
    property alias textColor: text.color

    id: control
    hoverEnabled: !ScreenTools.isMobile
    topPadding: _settingsLook ? ScreenTools.mockupUnit * 0.35 : _verticalPadding
    bottomPadding: _settingsLook ? ScreenTools.mockupUnit * 0.35 : _verticalPadding
    leftPadding: _settingsLook ? ScreenTools.mockupUnit * 1.1 : _horizontalPadding
    rightPadding: _settingsLook ? ScreenTools.mockupUnit * 1.1 : _horizontalPadding
    focusPolicy: Qt.ClickFocus
    font.family: ScreenTools.normalFontFamily
    text: ""

    property bool _showHighlight: enabled && (pressed | checked)
    property int _horizontalPadding: ScreenTools.defaultFontPixelWidth * 2
    property int _verticalPadding: Math.round(ScreenTools.defaultFontPixelHeight * heightFactor) - (iconSource === "" ? 0 : (_iconHeight - ScreenTools.defaultFontPixelHeight)  / 2)
    property real _iconHeight: text.height * 1.5
    /// Under the settings views, the police settings mockup's text button: 1.05 cqw text padded
    /// .35 cqw down and 1.1 cqw across, a hairline in the card's line and a .4 cqw corner
    readonly property bool _settingsLook: ScreenTools.inSettingsLook(control)
    // A finger's tap target around the drawn control
    containmentMask: _settingsLook ? _touchArea : null
    SettingsTouchArea { id: _touchArea; visible: control._settingsLook }

    QGCPalette { id: qgcPal; colorGroupEnabled: control.enabled }

    background: Rectangle {
        id: backRect
        radius: backRadius
        implicitWidth: ScreenTools.implicitButtonWidth
        implicitHeight: _settingsLook ? 0 : ScreenTools.implicitButtonHeight
        border.width: showBorder ? (_settingsLook ? ScreenTools.hairline : 1) : 0
        border.color: _settingsLook ? qgcPal.cardBorder : qgcPal.buttonBorder
        border.pixelAligned: !_settingsLook    // a whole-pixel snap would round the hairline away
        color: primary ? qgcPal.primaryButton : (_settingsLook ? qgcPal.card : qgcPal.button)

        Rectangle {
            anchors.fill: parent
            color: qgcPal.buttonHighlight
            opacity: _showHighlight ? 1 : control.enabled && control.hovered ? .2 : 0
            radius: parent.radius
        }
    }

    contentItem: RowLayout {
        spacing: ScreenTools.defaultFontPixelWidth

        QGCColoredImage {
            id: icon
            Layout.alignment: Qt.AlignHCenter
            source: control.iconSource
            height: _iconHeight
            width: height
            color: text.color
            fillMode: Image.PreserveAspectFit
            sourceSize.height: height
            visible: control.iconSource !== ""
        }

        QGCLabel {
            id: text
            Layout.alignment: Qt.AlignHCenter
            text: control.text
            font.pointSize: control.pointSize
            font.family: control.font.family
            font.weight: fontWeight
            color: _showHighlight ? qgcPal.buttonHighlightText : (primary ? qgcPal.primaryButtonText : qgcPal.buttonText)
            visible: control.text !== ""
        }
    }
}
