import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.AppSettings

Rectangle {
    id:     settingsView
    // The page beside the rail; the rail itself shares the top bar's window colour
    color:  qgcPal.settingsPanel
    z:      QGroundControl.zOrderTopMost

    /// The pages under here draw their SettingsGroupLayout cards after the police mockup
    readonly property bool settingsMockupLook: true

    readonly property real _defaultTextHeight:  ScreenTools.defaultFontPixelHeight
    readonly property real _verticalMargin:     _defaultTextHeight / 2

    property bool _first: true
    property bool _commingFromRIDSettings: false
    property int  _selectedPageIndex: -1
    property int  _selectedSectionIndex: -1
    property var  _expandedPages: ({})  // pageIndex -> bool
    property int  _expandedRevision: 0  // bumped to trigger re-evaluation
    property string _searchQuery: ""

    /// Name of whatever the right panel is showing. Mirrors the vehicle config header so the two
    /// setup screens read the same way.
    readonly property string _panelTitle: {
        if (_selectedPageIndex < 0) return ""
        var entry = settingsPagesModel.get(_selectedPageIndex)
        if (!entry) return ""
        if (_selectedSectionIndex >= 0) {
            var sections = _pageSections(entry)
            for (var i = 0; i < sections.length; i++) {
                if (sections[i].index === _selectedSectionIndex) return sections[i].name
            }
        }
        return entry.name ?? ""
    }

    function _setExpanded(pageIndex, value) {
        _expandedPages[pageIndex] = value
        _expandedRevision++
    }

    function _isExpanded(pageIndex) {
        void _expandedRevision  // create binding dependency
        return !!_expandedPages[pageIndex]
    }

    function _pageSections(entry) {
        return entry && typeof entry.sections === "function" ? entry.sections() : []
    }

    function _pageAvailable(entry) {
        if (!entry || entry.name === "Divider" ||
                (typeof entry.pageVisible === "function" && !entry.pageVisible())) {
            return false
        }

        var sections = _pageSections(entry)
        return sections.length === 0 || sections.some(function(section) { return section.visible })
    }

    function _sectionAvailable(sections, sectionIndex) {
        if (sectionIndex === -1) return true
        for (var i = 0; i < sections.length; i++) {
            if (sections[i].index === sectionIndex) return sections[i].visible
        }
        return false
    }

    // Search: returns array of matching section indices for a page, or empty if no match
    function _matchingSections(pageIndex) {
        var query = _searchQuery.toLowerCase().trim()
        if (query === "") return []  // empty = no filtering

        var entry = settingsPagesModel.get(pageIndex)
        if (!_pageAvailable(entry)) return []

        var sections = _pageSections(entry)
        var matches = []
        for (var i = 0; i < sections.length; i++) {
            if (!sections[i].visible) continue
            for (var j = 0; j < sections[i].searchTerms.length; j++) {
                if (sections[i].searchTerms[j].indexOf(query) !== -1) {
                    matches.push(sections[i].index)
                    break
                }
            }
        }
        return matches
    }

    // Does this page have any search matches? (or is search empty = show all)
    function _pageMatchesSearch(pageIndex) {
        if (_searchQuery.trim() === "") return true
        return _matchingSections(pageIndex).length > 0
    }

    function _navigateTo(pageIndex, sectionIndex) {
        var entry = settingsPagesModel.get(pageIndex)
        if (!entry || entry.name === "Divider") return
        if (!_pageAvailable(entry)) return
        if (!_sectionAvailable(_pageSections(entry), sectionIndex)) return

        var url = entry.url
        _selectedSectionIndex = sectionIndex

        if (_selectedPageIndex !== pageIndex) {
            _selectedPageIndex = pageIndex
            rightPanel.source = url
        }

        // Apply section filter after the page is loaded
        if (rightPanel.item && typeof rightPanel.item.sectionFilter !== "undefined") {
            rightPanel.item.sectionFilter = sectionIndex
        }
    }

    function _navigateToFirstAvailablePage() {
        for (var i = 0; i < settingsPagesModel.count; i++) {
            if (_pageAvailable(settingsPagesModel.get(i))) {
                _navigateTo(i, -1)
                return
            }
        }

        _selectedPageIndex = -1
        _selectedSectionIndex = -1
        rightPanel.source = ""
    }

    // settingsPage is the untranslated page name from SettingsPages.json
    function showSettingsPage(settingsPage) {
        for (var i = 0; i < settingsPagesModel.count; i++) {
            var entry = settingsPagesModel.get(i)
            if (entry && entry.nameKey === settingsPage) {
                _navigateTo(i, -1)
                break
            }
        }
    }

    // This need to block click event leakage to underlying map.
    DeadMouseArea {
        anchors.fill: parent
    }

    QGCPalette { id: qgcPal }

    Component.onCompleted: {
        // Find and select the default page
        var targetUrl = globals.commingFromRIDIndicator
            ? "qrc:/qml/QGroundControl/AppSettings/RemoteIDSettings.qml"
            : "qrc:/qml/QGroundControl/AppSettings/GeneralSettings.qml"
        globals.commingFromRIDIndicator = false

        for (var i = 0; i < settingsPagesModel.count; i++) {
            var entry = settingsPagesModel.get(i)
            if (entry && entry.url === targetUrl) {
                _navigateTo(i, -1)
                break
            }
        }

        if (_selectedPageIndex === -1) {
            _navigateToFirstAvailablePage()
        }
    }

    Connections {
        target: rightPanel
        function onLoaded() {
            if (rightPanel.item && typeof rightPanel.item.sectionFilter !== "undefined") {
                rightPanel.item.sectionFilter = _selectedSectionIndex
            }
        }
    }

    SettingsPagesModel { id: settingsPagesModel }

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
        width:              Math.max(buttonColumn.implicitWidth, settingsView.width * 0.22 - divider.width)
        anchors.topMargin:  ScreenTools.mockupUnit * 1.2
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        spacing:            ScreenTools.mockupUnit * 0.6

        QGCTextField {
            id:                 searchField
            objectName:         "settings_searchField"
            Layout.fillWidth:   true
            Layout.leftMargin:  ScreenTools.mockupUnit * 1.6
            Layout.rightMargin: ScreenTools.mockupUnit * 1.6
            // The rail rows' text size, and a field as tall as it was
            font.pointSize:     ScreenTools.mockupPointUnit * 1.35
            _verticalPadding:   ScreenTools.mockupUnit
            placeholderText:    qsTr("Search settings...")

            onTextChanged: {
                settingsView._searchQuery = text
            }
        }

        QGCFlickable {
            id:                 buttonList
            objectName:         "settings_buttonList"
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            contentHeight:      buttonColumn.height + _verticalMargin
            flickableDirection:  Flickable.VerticalFlick
            clip:               true

        ColumnLayout {
            id:         buttonColumn
            width:      buttonList.width
            spacing:    0

            Repeater {
                id:     buttonRepeater
                model:  settingsPagesModel

                ColumnLayout {
                    id:     pageColumn
                    spacing: 0
                    Layout.fillWidth: true

                    required property int index
                    required property var model

                    property string pageName:    model.name ?? ""
                    property string pageUrl:     model.url ?? ""
                    property string pageIconUrl: model.iconUrl ?? ""
                    property var    pageVisible: model.pageVisible ?? function() { return true }
                    property var    pageSections: pageVisible() ? settingsView._pageSections(model) : []
                    property var    visiblePageSections: pageSections.filter(function(section) {
                        return section.visible
                    })
                    property bool pageAvailable: pageVisible() &&
                                                 (pageSections.length === 0 || visiblePageSections.length > 0)
                    property bool isSelected: settingsView._selectedPageIndex === index
                    property bool hasMultipleSections: visiblePageSections.length > 1
                    property bool isSearching: settingsView._searchQuery.trim() !== ""
                    property bool matchesSearch: pageAvailable && settingsView._pageMatchesSearch(index)
                    property bool isExpanded: hasMultipleSections && (isSearching ? matchesSearch : settingsView._isExpanded(index))

                    onPageSectionsChanged: {
                        let sections = pageSections ?? []
                        let visibleCount = sections.filter(function(section) { return section.visible }).length
                        if (isSelected && pageAvailable && settingsView._selectedSectionIndex !== -1 &&
                                (visibleCount <= 1 ||
                                 !settingsView._sectionAvailable(sections, settingsView._selectedSectionIndex))) {
                            settingsView._navigateTo(index, -1)
                        }
                    }

                    onPageAvailableChanged: {
                        if (isSelected && !pageAvailable) {
                            settingsView._navigateToFirstAvailablePage()
                        }
                    }

                    visible: {
                        if (pageName === "Divider") return !isSearching
                        if (!pageAvailable) return false
                        if (isSearching) return matchesSearch
                        return true
                    }

                    // Divider
                    Item {
                        Layout.fillWidth: true
                        height: ScreenTools.defaultFontPixelHeight / 2
                        visible: pageName === "Divider"
                    }

                    // Page button
                    SettingsButton {
                        Layout.fillWidth: true
                        // A full touch target per row, and every row but the open page's dimmed
                        Layout.preferredHeight: Math.max(implicitHeight, ScreenTools.minTouchPixels)
                        textColor:     isSelected ? qgcPal.buttonText : qgcPal.secondaryText
                        objectName:    "settingsButton_" + (model.nameKey ?? pageName)
                        text:          pageName
                        icon.source:   pageIconUrl
                        expandable:    hasMultipleSections
                        expanded:      isExpanded
                        checked:       isSelected && settingsView._selectedSectionIndex === -1
                        visible:       pageName !== "Divider" && pageAvailable

                        onClicked: {
                            if (mainWindow.allowViewSwitch()) {
                                settingsView._navigateTo(index, -1)
                                if (hasMultipleSections) {
                                    // Toggle expand/collapse when re-clicking the same page
                                    if (isSelected && isExpanded) {
                                        settingsView._setExpanded(index, false)
                                    } else if (!isExpanded) {
                                        settingsView._setExpanded(index, true)
                                    }
                                }
                            }
                        }

                        onToggleExpand: {
                            if (!mainWindow.allowViewSwitch()) {
                                return
                            }
                            var expanding = !isExpanded
                            settingsView._setExpanded(index, expanding)
                            if (!expanding && isSelected) {
                                settingsView._navigateTo(index, -1)
                            }
                        }
                    }

                    // Section sub-items (indented, shown when page is expanded)
                    Repeater {
                        model: isExpanded ? visiblePageSections : []

                        Button {
                            id:             sectionBtn
                            Layout.fillWidth: true
                            // Text lined up with the page rows' text: row padding, icon and gap
                            padding:        ScreenTools.mockupUnit * 0.75
                            leftPadding:    ScreenTools.mockupUnit * 4.3
                            hoverEnabled:   !ScreenTools.isMobile

                            property int sectionIndex: modelData.index
                            property bool sectionChecked: pageColumn.isSelected && settingsView._selectedSectionIndex === sectionIndex
                            property bool sectionMatchesSearch: {
                                if (!pageColumn.isSearching) return true
                                var matches = settingsView._matchingSections(pageColumn.index)
                                return matches.indexOf(sectionIndex) !== -1
                            }
                            property color textColor: qgcPal.buttonText
                            visible: sectionMatchesSearch

                            // Selected like a page row, with a narrower stripe so the sub-item still reads
                            // as subordinate. Only drawn while checked, when the fill is opaque
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

                            contentItem: QGCLabel {
                                text:  modelData.name
                                color: sectionBtn.sectionChecked ? sectionBtn.textColor : qgcPal.secondaryText
                                font.pointSize: ScreenTools.mockupPointUnit * 1.2
                                horizontalAlignment: Text.AlignLeft
                            }

                            onClicked: {
                                if (mainWindow.allowViewSwitch()) {
                                    settingsView._navigateTo(pageColumn.index, sectionIndex)
                                }
                            }
                        }
                    }
                }
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
            anchors.right:          parent.right
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
    }

    //-- Panel Contents. The page pads itself 2.2 cqw across; the first caption sits 1.2 cqw under the title
    Loader {
        id:                     rightPanel
        objectName:             "settings_rightPanel"
        anchors.topMargin:      ScreenTools.mockupUnit * 1.2
        anchors.bottomMargin:   _verticalMargin
        anchors.left:           divider.right
        anchors.right:          parent.right
        anchors.top:            panelHeader.bottom
        anchors.bottom:         parent.bottom
    }
}
