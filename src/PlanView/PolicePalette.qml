pragma Singleton

import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Police delivery accent colours for the plan screen.
///
/// Kept out of QGCPalette on purpose: that table is shared with the flight and settings views, and
/// this branch owns neither. Everything here applies only to files inside src/PlanView.
QtObject {
    /// 경찰청 상징색 계열. 원색 남색(#0B2E63)은 어두운 배경(#222222) 위에서 거의 보이지 않아
    /// 테두리에는 한 단계 밝은 청색을 쓴다. 정확한 CI 값을 받으면 여기만 고치면 된다.
    readonly property color navy:        "#0B2E63"
    readonly property color blue:        "#2E62B8"
    readonly property color blueDim:     "#1E3E70"

    /// 화면의 강조색. 순정 QGC 의 하늘색(buttonHighlight #3A9BDC)을 쓰지 않는다 —
    /// 그 색이 이 제품을 QGC 로 보이게 하는 가장 큰 한 가지다.
    /// 어두운 배경에는 밝은 남색, 밝은(야외) 배경에는 짙은 남색을 쓴다.
    /// 한 색으로 두 테마를 모두 만족하는 값은 없다.
    readonly property color accentDark:  "#4A8CE8"   // #222222 위 6.1:1
    readonly property color accentLight: "#0B2E63"   // #ffffff 위 12.9:1

    /// 슬라이더 트랙이 최소·최대 라벨을 덮는 것을 막는다.
    ///
    /// QGCSlider 는 showBoundaryValues 일 때 라벨 높이만큼 implicitHeight 를 늘려 놓고,
    /// 트랙과 손잡이는 늘어난 전체 높이의 가운데에 놓는다(QGCSlider.qml:14, :27, :41).
    /// 그래서 트랙이 라벨 자리로 내려앉는다. 그 파일은 전 화면 공용이라 고칠 수 없으므로
    /// 인스턴스마다 bottomPadding 을 줘서 트랙이 위쪽 절반에서만 가운데를 잡게 한다.
    function styleSlider(fieldSlider) {
        if (!fieldSlider) {
            return
        }
        var slider = _findSlider(fieldSlider)
        if (slider) {
            slider.bottomPadding = Math.round(ScreenTools.defaultFontPixelHeight * 0.95)
        }
    }

    function _findSlider(item) {
        if (!item || !item.children) {
            return null
        }
        for (var i = 0; i < item.children.length; i++) {
            var child = item.children[i]
            if (child && child.showBoundaryValues !== undefined) {
                return child
            }
            var found = _findSlider(child)
            if (found) {
                return found
            }
        }
        return null
    }

    /// FactTextFieldSlider 하나를 통째로 손본다. 인자는 그 슬라이더 인스턴스다.
    function styleField(fieldSlider) {
        if (!fieldSlider) {
            return
        }
        styleTextField(fieldSlider.textField ? fieldSlider.textField.textField : null)
        styleSlider(fieldSlider)
    }

    readonly property real  borderWidth: 1
    readonly property real  radius:      3

    /// 선택 강조 막대(탭 밑줄 등) 두께. 글자 크기와 같이 커지면 선이 아니라 띠가 되므로 화면 픽셀로 둔다.
    readonly property real  accentWidth: 3

    /// 입력 필드 색. QGCPalette 의 textField 는 dark 테마에서도 #ffffff(글자 #000000)라
    /// 어두운 화면에서 흰 상자로 튄다. 그 표는 전 화면 공용이라 여기서 인스턴스별로만 덮어쓴다.
    readonly property color fieldBackground:         "#15181E"
    readonly property color fieldBackgroundDisabled: "#2B2B2B"
    readonly property color fieldText:               "#FFFFFF"
    readonly property color fieldTextDisabled:       "#8A8A8A"

    /// QGCTextField(전 화면 공용, 수정 금지) 인스턴스 하나의 배경만 어둡게 바꾼다.
    /// dark 테마에서 QGCTextField 의 border.width 는 0 이라 배경을 어둡게 하면 필드 경계가 사라진다.
    /// 그래서 두께만 1 로 올리고 색은 원래 바인딩(qgcPal.buttonBorder / 검증 실패 시 colorRed)을
    /// 그대로 둔다 — 입력 검증 실패 시 빨간 테두리가 계속 보인다.
    /// 포커스 시 전체 선택(selectAll)이 어두운 배경 위에서 안 보이게 되므로 선택색도 함께 바꾼다.
    function styleTextField(textField) {
        // QGCTextField 는 전 화면 공용이라 그 내부 구조에 기대고 있다. 상류가 background 를
        // 바꾸면 여기서 조용히 틀린 색을 칠하는 대신 아무것도 안 하고 넘어가게 한다.
        if (!textField || !textField.background) {
            console.warn("PolicePalette: QGCTextField structure changed, skipping field styling")
            return
        }
        textField.background.color = Qt.binding(function() {
            return textField.enabled ? fieldBackground : fieldBackgroundDisabled
        })
        textField.background.border.width = Qt.binding(function() {
            return textField.validationError ? 2 : borderWidth
        })
        textField.color = Qt.binding(function() {
            return textField.enabled ? fieldText : fieldTextDisabled
        })
        textField.selectionColor = blue
        textField.selectedTextColor = fieldText
    }
}
