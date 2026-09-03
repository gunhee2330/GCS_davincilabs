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
}
