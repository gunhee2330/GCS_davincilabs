#include "VideoManagerInitTest.h"

#ifdef QGC_GST_STREAMING

#include "VideoManager.h"

#include <QtCore/QRegularExpression>
#include <QtQuick/QQuickWindow>

#include "Fact.h"
#include "SettingsManager.h"
#include "SiyiCameraSettings.h"

void VideoManagerInitTest::init()
{
    UnitTest::init();

    static const QRegularExpression sGStreamerCriticalRe(
        QStringLiteral("cannot register existing type|"
                       "g_type_add_interface_static.*G_TYPE_IS_INSTANTIATABLE|"
                       "g_once_init_leave.*result != 0|"
                       "GStreamer initialization failed"));
    // These backend startup diagnostics are platform/runtime dependent and not
    // tied to a single deterministic call site in this fixture.
    ignoreLogMessage("Video.GStreamer.GStreamerLogging", QtCriticalMsg, sGStreamerCriticalRe);
}

void VideoManagerInitTest::_testQmlReadyBeforeBackendReady()
{
    VideoManager videoManager;
    QQuickWindow mainWindow;
    videoManager._mainWindow = &mainWindow;

    int createReceiversCount = 0;
    videoManager._createVideoReceiversForTest = [&createReceiversCount]() {
        ++createReceiversCount;
    };

    videoManager._initState = VideoManager::InitState::Pending;

    videoManager._initAfterQmlIsReady();
    QCOMPARE(videoManager._initState, VideoManager::InitState::QmlReady);
    QCOMPARE(createReceiversCount, 0);

    videoManager._onBackendInitComplete(true);
    QCOMPARE(videoManager._initState, VideoManager::InitState::Running);
    QCOMPARE(createReceiversCount, 1);

    expectLogMessage("Video.VideoManager", QtWarningMsg, QRegularExpression(QStringLiteral("_onBackendInitComplete: unexpected state")));
    videoManager._onBackendInitComplete(true);
    verifyExpectedLogMessage();
    QCOMPARE(createReceiversCount, 1);
}

void VideoManagerInitTest::_testBackendReadyBeforeQmlReady()
{
    VideoManager videoManager;
    QQuickWindow mainWindow;
    videoManager._mainWindow = &mainWindow;

    int createReceiversCount = 0;
    videoManager._createVideoReceiversForTest = [&createReceiversCount]() {
        ++createReceiversCount;
    };

    videoManager._initState = VideoManager::InitState::Pending;

    videoManager._onBackendInitComplete(true);
    QCOMPARE(videoManager._initState, VideoManager::InitState::BackendReady);
    QCOMPARE(createReceiversCount, 0);

    videoManager._initAfterQmlIsReady();
    QCOMPARE(videoManager._initState, VideoManager::InitState::Running);
    QCOMPARE(createReceiversCount, 1);

    expectLogMessage("Video.VideoManager", QtWarningMsg, QRegularExpression(QStringLiteral("_initAfterQmlIsReady: unexpected state")));
    videoManager._initAfterQmlIsReady();
    verifyExpectedLogMessage();
    QCOMPARE(createReceiversCount, 1);
}

void VideoManagerInitTest::_testBackendInitFailure()
{
    VideoManager videoManager;
    QQuickWindow mainWindow;
    videoManager._mainWindow = &mainWindow;

    int createReceiversCount = 0;
    videoManager._createVideoReceiversForTest = [&createReceiversCount]() {
        ++createReceiversCount;
    };

    videoManager._initState = VideoManager::InitState::Pending;

    expectLogMessage("Video.VideoManager", QtCriticalMsg, QRegularExpression(QStringLiteral("video initialization failed")));
    videoManager._onBackendInitComplete(false);
    verifyExpectedLogMessage();
    QCOMPARE(videoManager._initState, VideoManager::InitState::Failed);
    QCOMPARE(createReceiversCount, 0);

    expectLogMessage("Video.VideoManager", QtWarningMsg, QRegularExpression(QStringLiteral("QML ready but video init failed")));
    videoManager._initAfterQmlIsReady();
    verifyExpectedLogMessage();
    QCOMPARE(videoManager._initState, VideoManager::InitState::Failed);
    QCOMPARE(createReceiversCount, 0);
}

/// aiEnabled decides which stream the main window gets: _updateSettings() branches on it to swap
/// the stock feed for the AI module's annotated one. That makes it a video source setting, and a
/// source setting nothing listens to is a switch that does nothing - the operator turns AI on and
/// the main window stays where it was, then stays on the AI feed after turning it off.
///
/// This asserts the wiring rather than a restart because the wiring is what was missing: the
/// branch downstream of it was always correct, and no signal reached it.
void VideoManagerInitTest::_testAiEnabledIsWiredToTheSourceChange()
{
    SiyiCameraSettings *const siyiSettings = SettingsManager::instance()->siyiCameraSettings();
    QVERIFY(siyiSettings);

    VideoManager videoManager;
    QQuickWindow mainWindow;
    videoManager.init(&mainWindow);

    // disconnect() reports whether there was anything to disconnect, which is the question.
    QVERIFY(QObject::disconnect(siyiSettings->aiEnabled(), &Fact::rawValueChanged,
                                &videoManager, &VideoManager::_videoSourceChanged));

    // Same probe against a URL that was already wired, so a disconnect() that answers true for
    // everything would fail this test rather than pass it silently.
    QVERIFY(QObject::disconnect(siyiSettings->aiRtspUrl(), &Fact::rawValueChanged,
                                &videoManager, &VideoManager::_videoSourceChanged));
    QVERIFY(!QObject::disconnect(siyiSettings->aiRtspUrl(), &Fact::rawValueChanged,
                                 &videoManager, &VideoManager::_videoSourceChanged));
}

#else

void VideoManagerInitTest::init() { UnitTest::init(); QSKIP("GStreamer not enabled"); }
void VideoManagerInitTest::_testQmlReadyBeforeBackendReady() { QSKIP("GStreamer not enabled"); }
void VideoManagerInitTest::_testBackendReadyBeforeQmlReady() { QSKIP("GStreamer not enabled"); }
void VideoManagerInitTest::_testBackendInitFailure() { QSKIP("GStreamer not enabled"); }
void VideoManagerInitTest::_testAiEnabledIsWiredToTheSourceChange() { QSKIP("GStreamer not enabled"); }

#endif

UT_REGISTER_TEST(VideoManagerInitTest, TestLabel::Unit)
