import QtQuick

import QGroundControl
import QGroundControl.Controls

/// The stock slide-to-confirm control, hosted outside the stock toolbar.
///
/// GuidedActionConfirm sends every guided command from the onActivated of its hold button, and
/// stock QGC instantiates that component only inside FlyViewToolBar. The police layout hides that
/// toolbar, and a hidden parent neither draws its children nor gives them input, so takeoff, land,
/// RTL, pause and mission start raised a control that could not be reached. This holds a second
/// instance of the stock component where the police layout can reach it.
Item {
    id:         root
    objectName: "policeGuidedConfirmHost"

    // Off the context chain, the way FlyViewToolBar and every stock Guided* control reach it.
    property var _guidedController: globals.guidedControllerFlyView

    // Stock toolbar height, so the hold button has the touch area it was drawn for.
    width:  confirm.width
    height: ScreenTools.toolbarHeight

    // The value FlyView.qml gives the stock toolbar, which lays its message display out with it.
    readonly property real _margins: ScreenTools.defaultFontPixelWidth / 2

    /// Bottom of what this host is showing, in its own coordinate space, or 0 while it shows
    /// nothing. The message display hangs below this item and carries the same visibility as the
    /// control, so it is the lowest edge whenever there is one.
    readonly property real contentBottom: confirm.visible ? messageDisplay.y + messageDisplay.height : 0

    QGCPalette { id: qgcPal }

    // This instance and the hidden toolbar's both assign themselves to
    // guidedController.confirmDialog in their own Component.onCompleted, and QML fixes no order
    // between the two. The claim is taken back here afterwards rather than raced for. The !== guard
    // drops the write once the property already points here, so the handler cannot re-trigger
    // itself.
    Connections {
        target: root._guidedController

        function onConfirmDialogChanged() {
            if (root._guidedController.confirmDialog !== confirm) {
                root._guidedController.confirmDialog = confirm
            }
        }
    }

    // What the stock centerPanel draws behind the control.
    Rectangle {
        anchors.fill: parent
        color:        qgcPal.windowTransparent
        visible:      confirm.visible
    }

    GuidedActionConfirm {
        id:                       confirm
        anchors.horizontalCenter: parent.horizontalCenter
        height:                   parent.height
        guidedController:         root._guidedController
        // FlyView.qml hands the slider to the controller, which holds it as a property.
        guidedValueSlider:        root._guidedController.guidedValueSlider
        messageDisplay:           messageDisplay
    }

    // Below the control rather than inside it, as in the stock toolbar: it carries the sentence for
    // the pending action. The label is anchored at the left with margins so short text stays inside
    // this background.
    Rectangle {
        id:                       messageDisplay
        anchors.top:              root.bottom
        anchors.topMargin:        root._margins
        anchors.horizontalCenter: root.horizontalCenter
        width:                    messageLabel.contentWidth + (root._margins * 2)
        height:                   messageLabel.contentHeight + (root._margins * 2)
        color:                    qgcPal.windowTransparent
        radius:                   ScreenTools.defaultBorderRadius
        visible:                  confirm.visible

        QGCLabel {
            id:       messageLabel
            x:        root._margins
            y:        root._margins
            width:    ScreenTools.defaultFontPixelWidth * 30
            wrapMode: Text.WordWrap
            text:     confirm.message
        }
    }

    // GuidedActionConfirm.reset() reaches for these two by bare id, which resolves through the
    // context of the file that instantiates it. FlyViewToolBar declares them beside its own message
    // display; declaring the same two here leaves that stock file untouched.
    PropertyAnimation {
        id:       messageOpacityAnimation
        target:   messageDisplay
        property: "opacity"
        from:     1
        to:       0
        duration: 500
    }

    Timer {
        id:          messageFadeTimer
        interval:    4000
        onTriggered: messageOpacityAnimation.start()
    }
}
