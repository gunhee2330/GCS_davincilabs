#pragma once

#include "QmlUITestBase.h"

/// The 영상 기록 page (RFP p5, p6 차, p9), driven end to end: a PX4 MockLink for the aircraft's
/// position, a fake pod on loopback answering the SDK over UDP (ZT30, recording on and off) and its
/// media API over HTTP, the page opened from the menu, a video listed, downloaded under the RFP
/// name, played, exported and deleted behind the administrator PIN, and the 녹화 파일 이름 setting.
class PoliceRecordingsUITest : public QmlUITestBase
{
    Q_OBJECT

protected slots:
    /// The copies, index and recording log live in files of their own, which the per-test settings
    /// reset does not reach.
    void init() override;

private slots:
    /// The pod starting to record is logged with the aircraft's position. 영상 기록 sits in the menu
    /// under 미션 and opens the page titled 영상 기록 under the settings bar: the header with 암호화
    /// 저장 held off (암호화 모듈 준비 중), 자동 삭제 30일 and 접근 사용자 관리자, 운용자; the source
    /// with the pod's link (ZT30 연결됨); the pod's three videos newest first with the counts. 태블릿으로
    /// 받기 brings one in as yyyyMMddhhmmsszzz_lat_lon.mp4 with 받음; a stalled one shows how far it
    /// got; one with no recording logged says it is named by the pod's time. 재생 plays the copy. As
    /// the operator, 내보내기 and 삭제 ask for the PIN, which a wrong one does not pass. The settings'
    /// 녹화 group shows the name's form. Also the captures, when a capture directory is set.
    void _testRecordingsPage();

private:
    /// Pre-existing warnings the strict log check would otherwise fail on.
    void _ignorePreexistingQmlWarnings();

    /// Grab the window to <QGC_SCREENSHOT_DIR>/<name>.png after \a settleMs, when that is set.
    void _grabIfCapturing(const QString &name, int settleMs = 1500);
};
