#pragma once

#include "QmlUITestBase.h"

/// The 비행 기록 page (RFP p6 차, p6, p10), driven end to end on a PX4 MockLink: two flights flown
/// and landed, the page opened from the status drawer's 전체 기록, the days filtered, a row ticked
/// and exported, and the aircraft's own log fetched for the newest flight.
class PoliceFlightRecordsUITest : public QmlUITestBase
{
    Q_OBJECT

protected slots:
    /// The flight records live in files of their own, which the per-test settings reset does not reach.
    void init() override;

private slots:
    /// The drawer's 전체 기록 opens the page under the settings bar, titled 비행 기록, listing the
    /// last thirty days of every airframe newest first with the span's summary. 조회 over other
    /// days lists theirs. A ticked row is counted in the summary and is what 선택 내보내기 writes:
    /// a dated folder with its CSV line and its telemetry log. USB로 저장 with no stick in says
    /// so. 기체 로그 받기 goes 받기 전, 받는 중 N%, 받음 with the file's name. Also the captures,
    /// when a capture directory is set.
    void _testRecordsPage();

private:
    /// Pre-existing warnings the strict log check would otherwise fail on.
    void _ignorePreexistingQmlWarnings();

    /// Grab the window to <QGC_SCREENSHOT_DIR>/<name>.png after \a settleMs, when that is set.
    void _grabIfCapturing(const QString &name, int settleMs = 1500);
};
