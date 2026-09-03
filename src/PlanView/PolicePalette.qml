pragma Singleton

import QtQuick

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
