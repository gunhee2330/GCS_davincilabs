import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:             vehicleConfigView
    objectName:     "vehicleConfig_root"
    // The page beside the rail; the rail itself shares the top bar's window colour
    color:          qgcPal.settingsPanel
    z:      QGroundControl.zOrderTopMost

    // This need to block click event leakage to underlying map.
    DeadMouseArea {
        anchors.fill: parent
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    /// The pages under here draw their SettingsGroupLayout cards after the police mockup
    readonly property bool      settingsMockupLook: true

    readonly property real      _defaultTextHeight: ScreenTools.defaultFontPixelHeight
    readonly property real      _defaultTextWidth:  ScreenTools.defaultFontPixelWidth
    readonly property real      _verticalMargin:    _defaultTextHeight / 2
    readonly property real      _buttonWidth:       _defaultTextWidth * 18
    readonly property string    _armedVehicleText:  qsTr("This operation cannot be performed while the vehicle is armed.")
    // Unselected rail rows, dimmed as on the settings rail
    readonly property color     _railDimText:       qgcPal.secondaryText

    /// Name of whatever the right panel is showing. The sidebar selection scrolls out of view on a
    /// short screen, so the header is the only place the operator can read back where they are.
    readonly property string    _panelTitle: {
        switch (_selectedSpecial) {
        case "summary":     return qsTr("Summary")
        case "parameters":  return qsTr("Parameters")
        case "firmware":    return qsTr("Firmware")
        case "opticalflow": return qsTr("Optical Flow")
        }
        if (!_fullParameterVehicleAvailable) return ""
        var components = _activeVehicle.autopilotPlugin.vehicleComponents
        if (_selectedComponentIndex < 0 || _selectedComponentIndex >= components.length) return ""
        var component = components[_selectedComponentIndex]
        var sectionId = _sectionId(_selectedComponentIndex, _selectedSectionIndex)
        return sectionId !== "" ? _sectionDisplayName(component, sectionId) : component.name
    }

    property var    _activeVehicle:                 QGroundControl.multiVehicleManager.activeVehicle
    property bool   _vehicleArmed:                  _activeVehicle ? _activeVehicle.armed : false
    property string _messagePanelText:              qsTr("missing message panel text")
    property bool   _fullParameterVehicleAvailable: _activeVehicle && QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable && !_activeVehicle.parameterManager.missingParameters
    property var    _corePlugin:                    QGroundControl.corePlugin

    // Tree view state
    property int    _selectedComponentIndex: -1     // -1 = summary or special button
    property int    _selectedSectionIndex:   -1
    property string _selectedSpecial:        ""     // "summary", "parameters", "firmware", "opticalflow"
    property bool   _showingPrereqMessage:   false  // Panel area shows prerequisite message instead of selected component
    property var    _expandedComponents:     ({})
    property int    _expandedRevision:       0
    property string _searchQuery:            ""

    function _setExpanded(compIndex, value) {
        _expandedComponents[compIndex] = value
        _expandedRevision++
    }

    function _isExpanded(compIndex) {
        void _expandedRevision
        return !!_expandedComponents[compIndex]
    }

    /// True if a vehicle component may be shown to an operator. The RFP gives the operator every
    /// vehicle and flight setting, so every component is open. What stays behind developer mode is
    /// outside the components: the raw Parameters editor and Firmware buttons below, and the items
    /// inside individual pages that carry their own gate (CompassMot, sensor orientation).
    function _componentAllowed(comp) {
        return !!comp
    }

    /// Translated display name for a section ID. JSON-driven components translate via the JSON
    /// filename context; hand-coded components provide sectionDisplayName().
    function _sectionDisplayName(component, sectionId) {
        let context = _translationContext(component)
        if (context) {
            return qsTranslate(context, sectionId)
        }
        if (component && typeof component.sectionDisplayName === "function") {
            return component.sectionDisplayName(sectionId)
        }
        return sectionId
    }

    /// Get the section ID for a sidebar entry.
    function _sectionId(compIndex, sectionIndex) {
        if (sectionIndex < 0 || !_fullParameterVehicleAvailable) return ""
        var components = _activeVehicle.autopilotPlugin.vehicleComponents
        if (compIndex < 0 || compIndex >= components.length) return ""
        var secs = components[compIndex].sectionIds
        if (sectionIndex < secs.length) return secs[sectionIndex]
        return ""
    }

    /// Extract the translation context (JSON filename) from a component.
    function _translationContext(component) {
        if (!component || !component.vehicleConfigJson) return ""
        var path = component.vehicleConfigJson.toString()
        var slash = path.lastIndexOf("/")
        return slash >= 0 ? path.substring(slash + 1) : path
    }

    function _componentMatchesSearch(component) {
        if (_searchQuery.trim() === "") return true
        var query = _searchQuery.toLowerCase().trim()
        if (component.name.toLowerCase().indexOf(query) !== -1) return true
        var context = _translationContext(component)
        var secs = component.sectionIds
        if (secs) {
            for (var i = 0; i < secs.length; i++) {
                if (secs[i].toLowerCase().indexOf(query) !== -1) return true
                let displayName = _sectionDisplayName(component, secs[i])
                if (displayName !== secs[i] && displayName.toLowerCase().indexOf(query) !== -1) {
                    return true
                }
            }
        }
        var keywords = component.sectionKeywords
        if (keywords) {
            for (var key in keywords) {
                var terms = keywords[key]
                for (var j = 0; j < terms.length; j++) {
                    if (terms[j].toLowerCase().indexOf(query) !== -1) return true
                    if (context && qsTranslate(context, terms[j]).toLowerCase().indexOf(query) !== -1) return true
                }
            }
        }
        return false
    }

    function _sectionMatchesSearch(component, sectionId) {
        if (_searchQuery.trim() === "") return true
        var query = _searchQuery.toLowerCase().trim()
        if (sectionId.toLowerCase().indexOf(query) !== -1) return true
        var context = _translationContext(component)
        let displayName = _sectionDisplayName(component, sectionId)
        if (displayName !== sectionId && displayName.toLowerCase().indexOf(query) !== -1) {
            return true
        }
        var keywords = component.sectionKeywords
        if (keywords && keywords[sectionId]) {
            var terms = keywords[sectionId]
            for (var i = 0; i < terms.length; i++) {
                if (terms[i].toLowerCase().indexOf(query) !== -1) return true
                if (context && qsTranslate(context, terms[i]).toLowerCase().indexOf(query) !== -1) return true
            }
        }
        return false
    }

    function showSummaryPanel() {
        if (mainWindow.allowViewSwitch()) {
            _showSummaryPanel()
        }
    }

    function _showSummaryPanel() {
        _selectedSpecial = "summary"
        _selectedComponentIndex = -1
        _selectedSectionIndex = -1
        if (_fullParameterVehicleAvailable) {
            if (_activeVehicle.autopilotPlugin.vehicleComponents.length === 0) {
                panelLoader.setSourceComponent(noComponentsVehicleSummaryComponent)
            } else {
                panelLoader.setSource("qrc:/qml/QGroundControl/VehicleSetup/VehicleSummary.qml")
            }
        } else if (QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable) {
            panelLoader.setSourceComponent(missingParametersVehicleSummaryComponent)
        } else {
            panelLoader.setSourceComponent(disconnectedVehicleAndParamsSummaryComponent)
        }
    }

    function showPanel(specialName, qmlSource) {
        if (mainWindow.allowViewSwitch()) {
            _selectedSpecial = specialName
            _selectedComponentIndex = -1
            _selectedSectionIndex = -1
            panelLoader.setSource(qmlSource)
        }
    }

    function _navigateToComponent(compIndex, sectionIndex) {
        if (!mainWindow.allowViewSwitch()) return
        if (!_fullParameterVehicleAvailable) return

        var components = _activeVehicle.autopilotPlugin.vehicleComponents
        if (compIndex < 0 || compIndex >= components.length) return
        var vehicleComponent = components[compIndex]
        if (!_componentAllowed(vehicleComponent)) {
            _showSummaryPanel()
            return
        }

        _selectedSpecial = ""

        // If component opts in and root was clicked, auto-select first section
        if (sectionIndex < 0 && vehicleComponent.showFirstSectionOnRootClick && vehicleComponent.sectionIds.length > 0) {
            sectionIndex = 0
        }
        _selectedSectionIndex = sectionIndex

        var autopilotPlugin = _activeVehicle.autopilotPlugin
        var prereq = autopilotPlugin.prerequisiteSetup(vehicleComponent)
        if (prereq !== "") {
            // Selection state still updates so the tree expands and highlights
            // normally; only the panel area shows the prerequisite message
            _selectedComponentIndex = compIndex
            _showingPrereqMessage = true
            _messagePanelText = qsTr("%1 setup must be completed prior to %2 setup.").arg(prereq).arg(vehicleComponent.name)
            panelLoader.setSourceComponent(messagePanelComponent)
            return
        }

        if (_selectedComponentIndex !== compIndex || _showingPrereqMessage) {
            _selectedComponentIndex = compIndex
            _showingPrereqMessage = false
            panelLoader.setSource(vehicleComponent.setupSource, vehicleComponent)
        }

        // Apply section filter
        if (panelLoader.item && typeof panelLoader.item.sectionIdFilter !== "undefined") {
            panelLoader.item.sectionIdFilter = _sectionId(compIndex, sectionIndex)
        }
    }

    function showParametersPanel() {
        showPanel("parameters", "qrc:/qml/QGroundControl/VehicleSetup/SetupParameterEditor.qml")
    }

    function showVehicleComponentPanel(vehicleComponent) {
        if (!mainWindow.allowViewSwitch()) return
        if (!_fullParameterVehicleAvailable) return

        var components = _activeVehicle.autopilotPlugin.vehicleComponents
        for (var i = 0; i < components.length; i++) {
            if (components[i] === vehicleComponent) {
                _navigateToComponent(i, -1)
                return
            }
        }
    }

    Component.onCompleted: _showSummaryPanel()

    Connections {
        target: QGroundControl.corePlugin
        function onShowAdvancedUIChanged(showAdvancedUI) {
            if (!showAdvancedUI) {
                _showSummaryPanel()
            }
        }
    }

    Connections {
        target: QGroundControl.multiVehicleManager
        function onParameterReadyVehicleAvailableChanged(parametersReady) {
            if (parametersReady || _selectedSpecial === "summary" || _selectedSpecial !== "firmware") {
                _showSummaryPanel()
            }
        }
    }

    Connections {
        target: panelLoader
        function onLoaded() {
            if (panelLoader.item && typeof panelLoader.item.sectionIdFilter !== "undefined") {
                panelLoader.item.sectionIdFilter = _sectionId(_selectedComponentIndex, _selectedSectionIndex)
            }
        }
    }

    Component {
        id: noComponentsVehicleSummaryComponent
        Rectangle {
            color: qgcPal.windowShade
            QGCLabel {
                anchors.margins:        _defaultTextWidth * 2
                anchors.fill:           parent
                verticalAlignment:      Text.AlignVCenter
                horizontalAlignment:    Text.AlignHCenter
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.mediumFontPointSize
                text:                   qsTr("%1 does not currently support configuration of your vehicle. ").arg(QGroundControl.appName) +
                                        "If your vehicle is already configured you can still Fly."
            }
        }
    }

    Component {
        id: disconnectedVehicleAndParamsSummaryComponent
        Rectangle {
            id: disconnectedRect
            color: qgcPal.windowShade
            Column {
                anchors.centerIn:   parent
                spacing:            ScreenTools.defaultFontPixelHeight
                QGCLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width:              disconnectedRect.width - _defaultTextWidth * 4
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode:           Text.WordWrap
                    font.pointSize:     ScreenTools.largeFontPointSize
                    text:               !_activeVehicle
                                            ? qsTr("Vehicle configuration pages will display after you connect your vehicle and parameters have been downloaded.")
                                            : (_activeVehicle.parameterManager.parameterDownloadSkipped
                                                ? qsTr("Parameter download was skipped because the vehicle is flying. Configuration pages will be available after parameters are downloaded.")
                                                : qsTr("Waiting for vehicle parameters to download…"))
                }
                QGCButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text:       qsTr("Download Parameters")
                    visible:    _activeVehicle && _activeVehicle.parameterManager.parameterDownloadSkipped
                    enabled:    _activeVehicle && _activeVehicle.parameterManager.parameterDownloadSkipped && _activeVehicle.parameterManager.loadProgress === 0
                    onClicked:  _activeVehicle.parameterManager.refreshAllParameters()
                }
            }
        }
    }

    Component {
        id: missingParametersVehicleSummaryComponent

        Rectangle {
            color: qgcPal.windowShade

            QGCLabel {
                anchors.margins:        _defaultTextWidth * 2
                anchors.fill:           parent
                verticalAlignment:      Text.AlignVCenter
                horizontalAlignment:    Text.AlignHCenter
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.mediumFontPointSize
                text:                   qsTr("Vehicle did not return the full parameter list. ") +
                                        qsTr("As a result, the configuration pages are not available.")
            }
        }
    }

    Component {
        id: messagePanelComponent

        Item {
            objectName: "vehicleConfig_messagePanel"

            QGCLabel {
                anchors.margins:        _defaultTextWidth * 2
                anchors.fill:           parent
                verticalAlignment:      Text.AlignVCenter
                horizontalAlignment:    Text.AlignHCenter
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.mediumFontPointSize
                text:                   _messagePanelText
            }
        }
    }

    // Declared ahead of leftPanel so it stacks behind it. The mockup runs the rail in the top
    // bar's colour, a shade darker than the page it borders
    Rectangle {
        anchors.left:   parent.left
        anchors.right:  divider.right
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        color:          qgcPal.window
    }

    // The mockup rail: 22% of the screen, its right border included, rows running edge to edge
    ColumnLayout {
        id:                 leftPanel
        width:              Math.max(buttonColumn.implicitWidth, vehicleConfigView.width * 0.22 - divider.width)
        anchors.topMargin:  ScreenTools.mockupUnit * 1.2
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        spacing:            ScreenTools.mockupUnit * 0.6

        QGCTextField {
            id:                 searchField
            Layout.fillWidth:   true
            Layout.leftMargin:  ScreenTools.mockupUnit * 1.6
            Layout.rightMargin: ScreenTools.mockupUnit * 1.6
            // The rail rows' text size, and a field as tall as it was
            font.pointSize:     ScreenTools.mockupPointUnit * 1.35
            _verticalPadding:   ScreenTools.mockupUnit
            placeholderText:    qsTr("Search configuration...")
            visible:            _fullParameterVehicleAvailable

            onTextChanged: {
                vehicleConfigView._searchQuery = text
            }
        }

        QGCFlickable {
            objectName:         "vehicleConfig_sidebarFlickable"
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            contentHeight:      buttonColumn.height + _verticalMargin
            flickableDirection:  Flickable.VerticalFlick
            clip:               true

            ColumnLayout {
                id:         buttonColumn
                width:      parent.width
                spacing:    0

                // Summary button
                ConfigButton {
                    id:                 summaryButton
                    objectName:         "vehicleConfig_summary"
                    icon.source:        "/qmlimages/VehicleSummaryIcon.png"
                    checked:            vehicleConfigView._selectedSpecial === "summary"
                    text:               qsTr("Summary")
                    Layout.fillWidth:   true
                    Layout.preferredHeight: Math.max(implicitHeight, ScreenTools.minTouchPixels)
                    textColor:          checked ? qgcPal.buttonText : _railDimText
                    visible:            vehicleConfigView._searchQuery.trim() === ""

                    onClicked: showSummaryPanel()
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight / 2
                    visible: vehicleConfigView._searchQuery.trim() === ""
                }

                // Vehicle component tree
                Repeater {
                    id:     componentRepeater
                    model:  _fullParameterVehicleAvailable ? _activeVehicle.autopilotPlugin.vehicleComponents : 0

                    ColumnLayout {
                        id:             compColumn
                        spacing:        0
                        Layout.fillWidth: true

                        required property int index
                        required property var modelData

                        property var    comp:           modelData
                        property string compName:       comp ? comp.name : ""
                        property var    compSectionIds: comp ? comp.sectionIds : []
                        property bool   isSelected:     vehicleConfigView._selectedComponentIndex === index && vehicleConfigView._selectedSpecial === ""
                        property bool   hasSections:    compSectionIds.length > 1
                        property bool   isSearching:    vehicleConfigView._searchQuery.trim() !== ""
                        property bool   matchesSearch:  comp ? vehicleConfigView._componentMatchesSearch(comp) : false
                        property bool   isExpanded:     hasSections && (isSearching ? matchesSearch : vehicleConfigView._isExpanded(index))

                        visible: {
                            void vehicleConfigView._corePlugin.showAdvancedUI // re-bind when maintenance mode toggles
                            if (!comp) return false
                            if (comp.setupSource.toString() === "") return false
                            if (!vehicleConfigView._componentAllowed(comp)) return false
                            if (isSearching) return matchesSearch
                            return true
                        }

                        ConfigButton {
                            Layout.fillWidth:   true
                            Layout.preferredHeight: Math.max(implicitHeight, ScreenTools.minTouchPixels)
                            textColor:          compColumn.isSelected ? qgcPal.buttonText : vehicleConfigView._railDimText
                            objectName:         "vehicleConfig_comp_" + compColumn.compName.replace(/ /g, "")
                            icon.source:        compColumn.comp ? compColumn.comp.iconResource : ""
                            setupComplete:      compColumn.comp ? compColumn.comp.setupComplete : true
                            text:               compColumn.compName
                            expandable:         compColumn.hasSections
                            expanded:           compColumn.isExpanded
                            checked:            compColumn.isSelected && vehicleConfigView._selectedSectionIndex === -1

                            onClicked: {
                                vehicleConfigView._navigateToComponent(compColumn.index, -1)
                                if (compColumn.hasSections) {
                                    if (compColumn.isSelected && compColumn.isExpanded) {
                                        vehicleConfigView._setExpanded(compColumn.index, false)
                                    } else if (!compColumn.isExpanded) {
                                        vehicleConfigView._setExpanded(compColumn.index, true)
                                    }
                                }
                            }

                            onToggleExpand: {
                                if (!mainWindow.allowViewSwitch()) return
                                var expanding = !compColumn.isExpanded
                                vehicleConfigView._setExpanded(compColumn.index, expanding)
                                if (!expanding && compColumn.isSelected) {
                                    vehicleConfigView._navigateToComponent(compColumn.index, -1)
                                }
                            }
                        }

                        // Section sub-items
                        Repeater {
                            model: compColumn.isExpanded ? compColumn.compSectionIds : []

                            Button {
                                id:             sectionBtn
                                objectName:     "vehicleConfig_section_" + modelData.replace(/ /g, "")
                                Layout.fillWidth: true
                                // Text lined up with the component rows' text: row padding, icon and gap
                                padding:        ScreenTools.mockupUnit * 0.75
                                leftPadding:    ScreenTools.mockupUnit * 4.3
                                hoverEnabled:   !ScreenTools.isMobile

                                property int sectionIndex: index
                                property bool sectionChecked: compColumn.isSelected && vehicleConfigView._selectedSectionIndex === sectionIndex
                                property bool sectionSetupComplete: {
                                    if (!compColumn.comp) {
                                        return true
                                    }
                                    // Referencing comp.setupComplete re-evaluates this binding whenever a
                                    // setup trigger parameter changes, since sectionSetupComplete() is a
                                    // plain function call which QML cannot otherwise track
                                    void compColumn.comp.setupComplete
                                    return typeof compColumn.comp.sectionSetupComplete === "function"
                                               ? compColumn.comp.sectionSetupComplete(modelData)
                                               : true
                                }
                                property bool sectionMatchesSearch: {
                                    if (!compColumn.isSearching) return true
                                    return vehicleConfigView._sectionMatchesSearch(compColumn.comp, modelData)
                                }
                                property bool sectionContentVisible: {
                                    if (!compColumn.isSelected) return true
                                    if (!panelLoader.item) return true
                                    if (typeof panelLoader.item.sectionVisible !== "function") return true
                                    return panelLoader.item.sectionVisible(modelData)
                                }
                                property color textColor: qgcPal.buttonText
                                visible: sectionMatchesSearch && sectionContentVisible

                                // Selected like a component row, with a narrower stripe so the sub-item still
                                // reads as subordinate. Only drawn while checked, when the fill is opaque
                                background: Rectangle {
                                    color:   qgcPal.selectedRow
                                    opacity: sectionBtn.sectionChecked || sectionBtn.pressed ? 1 : sectionBtn.enabled && sectionBtn.hovered ? 0.5 : 0

                                    Rectangle {
                                        anchors.left:   parent.left
                                        anchors.top:    parent.top
                                        anchors.bottom: parent.bottom
                                        width:          ScreenTools.mockupUnit * 0.2
                                        color:          qgcPal.buttonHighlight
                                        visible:        sectionBtn.sectionChecked
                                    }
                                }

                                contentItem: RowLayout {
                                    spacing: ScreenTools.defaultFontPixelWidth * 0.5

                                    Rectangle {
                                        width:   ScreenTools.defaultFontPixelWidth
                                        height:  width
                                        radius:  width / 2
                                        color:   sectionBtn.sectionSetupComplete ? qgcPal.colorGreen : qgcPal.colorOrange
                                        visible: !sectionBtn.sectionSetupComplete
                                    }

                                    QGCLabel {
                                        text:  vehicleConfigView._sectionDisplayName(compColumn.comp, modelData)
                                        color: sectionBtn.sectionChecked ? sectionBtn.textColor : qgcPal.secondaryText
                                        font.pointSize: ScreenTools.mockupPointUnit * 1.2
                                        horizontalAlignment: Text.AlignLeft
                                        Layout.fillWidth: true
                                    }
                                }

                                onClicked: {
                                    vehicleConfigView._navigateToComponent(compColumn.index, sectionIndex)
                                }
                            }
                        }
                    }
                }

                // Optical Flow (special)
                ConfigButton {
                    visible:            _corePlugin.showAdvancedUI &&
                                        (_activeVehicle ? _activeVehicle.flowImageIndex > 0 : false)
                    text:               qsTr("Optical Flow")
                    Layout.fillWidth:   true
                    Layout.preferredHeight: Math.max(implicitHeight, ScreenTools.minTouchPixels)
                    textColor:          checked ? qgcPal.buttonText : _railDimText
                    checked:            vehicleConfigView._selectedSpecial === "opticalflow"
                    onClicked:          showPanel("opticalflow", "qrc:/qml/QGroundControl/VehicleSetup/OpticalFlowSensor.qml")
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight / 2
                    visible: vehicleConfigView._searchQuery.trim() === ""
                }

                ConfigButton {
                    id:                 parametersButton
                    objectName:         "vehicleConfig_parametersButton"
                    visible:            QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable &&
                                        !_activeVehicle.usingHighLatencyLink &&
                                        _corePlugin.showAdvancedUI &&
                                        vehicleConfigView._searchQuery.trim() === ""
                    text:               qsTr("Parameters")
                    Layout.fillWidth:   true
                    Layout.preferredHeight: Math.max(implicitHeight, ScreenTools.minTouchPixels)
                    textColor:          checked ? qgcPal.buttonText : _railDimText
                    icon.source:        "/qmlimages/subMenuButtonImage.png"
                    checked:            vehicleConfigView._selectedSpecial === "parameters"
                    onClicked:          showPanel("parameters", "qrc:/qml/QGroundControl/VehicleSetup/SetupParameterEditor.qml")
                }

                // Upstream hides this on mobile. The police station is a handheld with a USB
                // host port, and the upgrader's port layer already has Android board detection,
                // so the tab is offered there too: the flight controller plugs into the
                // controller's USB-A. Whether the bootloader handshake survives Android's
                // re-enumeration permission prompt is being trialled on the UniRC 7 Pro.
                ConfigButton {
                    id:                 firmwareButton
                    icon.source:        "/qmlimages/FirmwareUpgradeIcon.png"
                    visible:            _corePlugin.showAdvancedUI &&
                                        _corePlugin.options.showFirmwareUpgrade &&
                                        vehicleConfigView._searchQuery.trim() === ""
                    text:               qsTr("Firmware")
                    Layout.fillWidth:   true
                    Layout.preferredHeight: Math.max(implicitHeight, ScreenTools.minTouchPixels)
                    textColor:          checked ? qgcPal.buttonText : _railDimText
                    checked:            vehicleConfigView._selectedSpecial === "firmware"

                    onClicked: showPanel("firmware", "qrc:/qml/QGroundControl/VehicleSetup/FirmwareUpgrade.qml")
                }
            }
        }
    }

    // Full height and drawn in the border colour: it is the rail's right edge, not a floating rule
    Rectangle {
        id:                     divider
        anchors.left:           leftPanel.right
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        width:                  ScreenTools.hairline
        color:                  "transparent"

        // A fill this thin is rounded up to a whole logical pixel by the software renderer; one
        // scaled down from a whole pixel lands on a single device pixel
        Rectangle {
            width:      1
            height:     parent.height
            color:      qgcPal.cardBorder
            transform:  Scale { xScale: ScreenTools.hairline }
            opacity:    0.999    // not opaque, or that renderer skips what lies under its whole-pixel bounds
        }
    }

    // Mockup panel: padded 1.6 cqw down and 2.2 cqw across, a 1.55 cqw semibold title
    Item {
        id:                 panelHeader
        anchors.left:       divider.right
        anchors.right:      parent.right
        anchors.top:        parent.top
        visible:            _panelTitle !== ""
        height:             visible ? _titleMetrics.height + ScreenTools.mockupUnit * 1.6 : 0

        // The title's line keeps the normal font's metrics, so a Korean title drawn in
        // NanumGothic sits where the page below expects it
        FontMetrics {
            id:             _titleMetrics
            font.family:    ScreenTools.normalFontFamily
            font.pointSize: ScreenTools.mockupPointUnit * 1.55
        }

        QGCLabel {
            id:                     panelTitleLabel
            anchors.leftMargin:     ScreenTools.mockupUnit * 2.2
            anchors.left:           parent.left
            anchors.rightMargin:    ScreenTools.mockupUnit * 2.2
            // A hidden item still has geometry, so anchoring to the pill unconditionally would
            // shorten the title by the pill's width even when disarmed
            anchors.right:          armedPill.visible ? armedPill.left : parent.right
            anchors.baseline:       parent.bottom
            anchors.baselineOffset: -_titleMetrics.descent
            text:                   _panelTitle
            font.pointSize:         ScreenTools.mockupPointUnit * 1.55
            // The mockup's 600. Under a Korean locale the title takes the bundled NanumGothic
            // (loaded for that locale), whose Bold is its 600: Open Sans falls back to a system
            // face for Hangul that draws no heavier weight at all
            font.family:            Qt.locale().name.startsWith("ko") ? "NanumGothic" : ScreenTools.normalFontFamily
            font.weight:            Font.DemiBold
            elide:                  Text.ElideRight
        }

        // Same armed condition the panels already refuse edits on, surfaced once at the top so the
        // operator sees why the fields below will not take input
        Rectangle {
            id:                     armedPill
            anchors.rightMargin:    ScreenTools.mockupUnit * 2.2
            anchors.right:          parent.right
            anchors.verticalCenter: panelTitleLabel.verticalCenter
            width:                  armedPillLabel.implicitWidth + _defaultTextHeight * 1.2
            height:                 armedPillLabel.implicitHeight + _defaultTextHeight * 0.45
            radius:                 height / 2
            color:                  qgcPal.window
            border.width:           1
            border.color:           qgcPal.groupBorder
            visible:                _vehicleArmed

            QGCLabel {
                id:                 armedPillLabel
                anchors.centerIn:   parent
                text:               qsTr("Locked while armed")
                font.bold:          true
                opacity:            0.7
            }
        }
    }

    Loader {
        id:                     panelLoader
        objectName:             "vehicleConfig_panelLoader"
        // The pages pad themselves by a character or so; this brings their content to the
        // mockup's 2.2 cqw, lined up under the title
        anchors.topMargin:      ScreenTools.mockupUnit * 1.2
        anchors.bottomMargin:   _verticalMargin
        anchors.leftMargin:     ScreenTools.mockupUnit * 2.2 - ScreenTools.defaultFontPixelWidth
        anchors.rightMargin:    ScreenTools.mockupUnit * 2.2 - ScreenTools.defaultFontPixelWidth
        anchors.left:           divider.right
        anchors.right:          parent.right
        anchors.top:            panelHeader.bottom
        anchors.bottom:         parent.bottom

        function setSource(source, vehicleComponent) {
            panelLoader.source = ""
            panelLoader.vehicleComponent = vehicleComponent
            panelLoader.source = source
        }

        function setSourceComponent(sourceComponent, vehicleComponent) {
            panelLoader.sourceComponent = undefined
            panelLoader.vehicleComponent = vehicleComponent
            panelLoader.sourceComponent = sourceComponent
        }

        property var vehicleComponent
    }
}
