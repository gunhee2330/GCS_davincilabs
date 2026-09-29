#include "PoliceRecordingsUITest.h"

#include <functional>

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtGui/QImage>
#include <QtNetwork/QUdpSocket>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "AppSettings.h"
#include "FakePodMediaServer.h"
#include "Fact.h"
#include "MockLink.h"
#include "PinGate.h"
#include "QGCCorePlugin.h"
#include "SettingsManager.h"
#include "SiyiCameraController.h"
#include "SiyiCameraSettings.h"
#include "SiyiProtocol.h"
#include "SiyiRecordings.h"
#include "Vehicle.h"

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

UT_REGISTER_TEST(PoliceRecordingsUITest, TestLabel::Integration)

namespace {

constexpr int kSettleMs = 1500;

/// The tablet's 1920x1200 under QT_SCALE_FACTOR=2.5, as the other police UI tests size it.
constexpr int kLayoutWidth  = 768;
constexpr int kLayoutHeight = 480;

void collectTexts(QQuickItem *item, QStringList &out)
{
    if (!item || !item->isVisible()) {
        return;
    }
    const QVariant text = item->property("text");
    if (text.isValid() && !text.toString().isEmpty()) {
        out.append(text.toString());
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *const child : children) {
        collectTexts(child, out);
    }
}

QString joinedTexts(QQuickItem *item)
{
    QStringList texts;
    collectTexts(item, texts);
    return texts.join(QLatin1Char('|'));
}

/// The pod's SDK side on loopback: a ZT30 answering its identity and config polls, recording or not.
class FakeGimbal : public QObject
{
public:
    bool recording = false;

    bool open()
    {
        if (!_socket.bind(QHostAddress::LocalHost, 0)) {
            return false;
        }
        (void) connect(&_socket, &QUdpSocket::readyRead, this, [this]() {
            while (_socket.hasPendingDatagrams()) {
                QByteArray datagram(static_cast<int>(_socket.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress sender;
                quint16 senderPort = 0;
                (void) _socket.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
                QByteArray buffer = datagram;
                for (const SiyiProtocol::Frame &frame : SiyiProtocol::decode(buffer)) {
                    QByteArray reply;
                    if (frame.commandId == SiyiProtocol::CommandId::AcquireHardwareId) {
                        reply = SiyiProtocol::encodeRaw(static_cast<quint8>(frame.commandId), QByteArray("7A"));
                    } else if (frame.commandId == SiyiProtocol::CommandId::AcquireConfigInfo) {
                        QByteArray config(7, '\0');
                        config[3] = recording ? 1 : 0;
                        config[5] = 1;
                        reply = SiyiProtocol::encodeRaw(static_cast<quint8>(frame.commandId), config);
                    }
                    if (!reply.isEmpty()) {
                        (void) _socket.writeDatagram(reply, sender, senderPort);
                    }
                }
            }
        });
        return true;
    }

    quint16 port() const { return _socket.localPort(); }

private:
    QUdpSocket _socket;
};

/// Waits for \a done, also running the main dispatch queue on macOS: the offscreen platform's
/// event loop never does, and AVFoundation, which the player loads through there, answers on it.
bool waitWithMainQueue(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
#ifdef Q_OS_MACOS
        (void) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
#endif
    }
    return true;
}

QByteArray sampleVideo()
{
    QFile file(QStringLiteral(":/unittest/pod_sample.mp4"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace

void PoliceRecordingsUITest::init()
{
    QVERIFY(QDir(SiyiRecordings::storeDirectory()).removeRecursively());
    SiyiRecordings *const recordings = SiyiRecordings::instance();
    QVERIFY(recordings);
    recordings->_index = QJsonObject();
    recordings->_events = QJsonArray();
    recordings->_pod.clear();
    QmlUITestBase::init();
}

void PoliceRecordingsUITest::_ignorePreexistingQmlWarnings()
{
    // The same pre-existing warnings PoliceTopBarUITest lists, none from anything under test.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/FlightMap/Widgets/PhotoVideoControl\\.qml:[0-9]+: "
                         "TypeError: Cannot read property '[A-Za-z0-9_]+' of (null|undefined)$")));
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^Both point size and pixel size set\\. Using pixel size\\.$")));
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^QString::arg: Argument missing: \"") +
                                        QRegularExpression::escape(QCoreApplication::translate(
                                            "PIDTuning", "Switches to '%1' when you click Stop.")) +
                                        QStringLiteral("\", ")));
    ignoreLogMessage("default", QtInfoMsg,
                     QRegularExpression(QStringLiteral("^qrc:/qml/QGroundControl/PlanView/HomePositionMapVisual\\.qml: ")));
}

void PoliceRecordingsUITest::_grabIfCapturing(const QString &name, int settleMs)
{
    const QString dir = qEnvironmentVariable("QGC_SCREENSHOT_DIR");
    if (dir.isEmpty()) {
        return;
    }
    QVERIFY2(QDir().mkpath(dir), qPrintable(QStringLiteral("Cannot create %1").arg(dir)));
    QTest::qWait(settleMs);
    const QImage image = _window->grabWindow();
    QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("Empty grab for %1").arg(name)));
    const QString path = QDir(dir).filePath(name + QStringLiteral(".png"));
    QVERIFY2(image.save(path), qPrintable(QStringLiteral("Cannot write %1").arg(path)));
}

void PoliceRecordingsUITest::_testRecordingsPage()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QVERIFY(QGCCorePlugin::instance()->setProperty("showAdvancedUI", false));

        // The pod: its SDK on UDP, then the recording log following it, as SiyiCameraController::init() starts it.
        FakeGimbal gimbal;
        QVERIFY(gimbal.open());
        SiyiCameraSettings *const podSettings = SettingsManager::instance()->siyiCameraSettings();
        QVERIFY(podSettings);
        podSettings->ipAddress()->setRawValue(QStringLiteral("127.0.0.1"));
        podSettings->port()->setRawValue(gimbal.port());
        SiyiCameraController *const pod = SiyiCameraController::instance();
        QVERIFY(pod);
        SiyiRecordings *const recordings = SiyiRecordings::instance();
        QVERIFY(recordings);
        recordings->init();
        pod->start();
        QVERIFY_TRUE_WAIT(pod->connected() && pod->model() == QStringLiteral("ZT30"), TestTimeout::mediumMs());
        QVERIFY_TRUE_WAIT(vehicle->coordinate().isValid(), TestTimeout::mediumMs());

        // Idle for a config round, then recording: one start logged, where the aircraft is.
        QTest::qWait(kSettleMs);
        QCOMPARE(recordings->events().size(), 0);
        gimbal.recording = true;
        QVERIFY_TRUE_WAIT(recordings->events().size() == 1, TestTimeout::mediumMs());
        const QJsonObject event = recordings->events().first().toObject();
        QVERIFY(event.contains(QStringLiteral("lat")));
        QCOMPARE(event.value(QStringLiteral("lat")).toDouble(), vehicle->coordinate().latitude());
        const qint64 startMs = event.value(QStringLiteral("ms")).toInteger();
        const QDateTime start = QDateTime::fromMSecsSinceEpoch(startMs);

        // Its media API: this recording, named by the pod with its start, one it wrote an hour
        // ago with no time in its name and nothing logged (held half sent), and yesterday's.
        const QByteArray video = sampleVideo();
        QVERIFY(!video.isEmpty());
        FakePodMediaServer media;
        const QString today = start.toString(QStringLiteral("yyyyMMdd"));
        const QString yesterday = start.addDays(-1).toString(QStringLiteral("yyyyMMdd"));
        media.files = {
            { today, QStringLiteral("REC_%1.mp4").arg(start.toString(QStringLiteral("yyyyMMdd_hhmmss"))), video, start.addSecs(2), false },
            { today, QStringLiteral("VID_0030.MP4"), QByteArray(400000, 'v'), start.addSecs(-3600), true },
            { yesterday, QStringLiteral("VID_0029.MP4"), video, start.addDays(-1), false },
        };
        QVERIFY(media.listen());
        recordings->_mediaPort = media.port();

        // 영상 기록, under 미션 in the menu. The police bar's hamburger has no objectName to tap it
        // by, so the menu is opened the way that button opens it.
        QVERIFY2(QMetaObject::invokeMethod(_window, "showToolSelectDialog"), "Could not open the menu");
        QQuickItem *const entry = findVisibleItem(_rootItem, QStringLiteral("toolbar_viewRecordings"), 3000);
        QVERIFY2(entry, "The menu has no 영상 기록");
        QQuickItem *const mission = findVisibleItem(_rootItem, QStringLiteral("toolbar_viewPlan"), 1000);
        QVERIFY2(mission, "The menu has no 미션");
        QVERIFY2(joinedTexts(entry).contains(QStringLiteral("영상 기록")), qPrintable(joinedTexts(entry)));
        const qreal entryTop = entry->mapToScene(QPointF(0, 0)).y();
        const qreal missionBottom = mission->mapToScene(QPointF(0, mission->height())).y();
        QVERIFY2(entryTop >= missionBottom && entryTop - missionBottom < entry->height(), "영상 기록 is not right under 미션");
        _grabIfCapturing(QStringLiteral("pr_0_menu"));

        QVERIFY2(clickButton(QStringLiteral("toolbar_viewRecordings")), "Could not tap 영상 기록");
        QQuickItem *const page = findVisibleItem(_rootItem, QStringLiteral("policeRecordingsPage"), 5000);
        QVERIFY2(page, "영상 기록 opened no page");
        QQuickItem *const toolDrawer = findVisibleItem(_rootItem, QStringLiteral("mainView_toolDrawer"), 1000);
        QVERIFY2(toolDrawer, "The page is not in the tool drawer");
        QCOMPARE(toolDrawer->property("toolTitle").toString(), QStringLiteral("영상 기록"));

        // The header: encryption held off and said why, 30 days, both users.
        QQuickItem *const encrypt = findVisibleItem(_rootItem, QStringLiteral("recordingsEncrypt"), 1000);
        QVERIFY2(encrypt, "No 암호화 저장 switch");
        QVERIFY(!encrypt->isEnabled());
        QVERIFY(!encrypt->property("checked").toBool());
        QQuickItem *const encryptNote = findVisibleItem(_rootItem, QStringLiteral("recordingsEncryptNote"), 1000);
        QVERIFY2(encryptNote, "No note by the encryption switch");
        QCOMPARE(encryptNote->property("text").toString(), QStringLiteral("암호화 모듈 준비 중"));
        QQuickItem *const autoDelete = findVisibleItem(_rootItem, QStringLiteral("recordingsAutoDelete"), 1000);
        QVERIFY2(autoDelete, "No 자동 삭제");
        QCOMPARE(autoDelete->property("currentText").toString(), QStringLiteral("30일"));
        QQuickItem *const access = findVisibleItem(_rootItem, QStringLiteral("recordingsAccess"), 1000);
        QVERIFY2(access, "No 접근 사용자");
        QCOMPARE(access->property("currentText").toString(), QStringLiteral("관리자, 운용자"));

        QQuickItem *const podState = findVisibleItem(_rootItem, QStringLiteral("recordingsPodState"), 1000);
        QVERIFY2(podState, "No pod link state");
        QCOMPARE(podState->property("text").toString(), QStringLiteral("ZT30 연결됨"));

        QQuickItem *const list = findVisibleItem(_rootItem, QStringLiteral("recordingsList"), 1000);
        QVERIFY2(list, "No list");
        QVERIFY_TRUE_WAIT(list->property("count").toInt() == 3, TestTimeout::mediumMs());
        QQuickItem *const summary = findVisibleItem(_rootItem, QStringLiteral("recordingsSummary"), 1000);
        QVERIFY2(summary, "No summary line");
        QVERIFY_TRUE_WAIT(summary->property("text").toString() == QStringLiteral("카메라에 녹화 파일 3개, 태블릿에 받은 파일 0개, 최신순"), 3000);

        // Rows and what is in them are looked up afresh each time: a list reshaped under a held
        // pointer would leave it dangling.
        const auto rowText = [&](int index) {
            QQuickItem *const row = findVisibleItem(_rootItem, QStringLiteral("recordingsRow_%1").arg(index), 0);
            return row ? joinedTexts(row) : QString();
        };
        const auto textOf = [&](const QString &objectName) {
            QQuickItem *const item = findVisibleItem(_rootItem, objectName, 0);
            return item ? item->property("text").toString() : QString();
        };
        QVERIFY_TRUE_WAIT(!rowText(2).isEmpty(), 3000);
        QVERIFY2(rowText(0).contains(QStringLiteral("카메라 파일 REC_")) && rowText(0).contains(QStringLiteral("받기 전")) &&
                     rowText(0).contains(QStringLiteral("녹화 시작 ") + start.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))),
                 qPrintable(rowText(0)));
        QVERIFY2(rowText(1).contains(QStringLiteral("카메라 파일 VID_0030.MP4")) && rowText(1).contains(QStringLiteral("(녹화 기록 없음)")),
                 qPrintable(rowText(1)));
        _grabIfCapturing(QStringLiteral("pr_1_page"));

        // 태블릿으로 받기: the RFP name with the logged start and position, and 받음.
        const QString expected = SiyiRecordings::rfpName(startMs, true, event.value(QStringLiteral("lat")).toDouble(),
                                                         event.value(QStringLiteral("lon")).toDouble());
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{17}_-?\\d+\\.\\d{4}_-?\\d+\\.\\d{4}\\.mp4$")).match(expected).hasMatch());
        QVERIFY2(clickButton(QStringLiteral("recordingsFetch_0")), "Could not tap 태블릿으로 받기");
        QVERIFY_TRUE_WAIT(textOf(QStringLiteral("recordingsName_0")) == expected, TestTimeout::mediumMs());
        QVERIFY2(rowText(0).contains(QStringLiteral("|받음|")), qPrintable(rowText(0)));
        QCOMPARE(QFile(QDir(SiyiRecordings::storeDirectory()).filePath(expected)).size(), video.size());
        QVERIFY_TRUE_WAIT(summary->property("text").toString() == QStringLiteral("카메라에 녹화 파일 3개, 태블릿에 받은 파일 1개, 최신순"), 3000);

        QQuickItem *const detail = findVisibleItem(_rootItem, QStringLiteral("recordingsDetail"), 1000);
        QVERIFY2(detail, "No detail pane");
        const QString detailText = joinedTexts(detail);
        QVERIFY2(detailText.contains(expected) && detailText.contains(QStringLiteral("촬영 위치|%1, %2")
                                                                          .arg(event.value(QStringLiteral("lat")).toDouble(), 0, 'f', 4)
                                                                          .arg(event.value(QStringLiteral("lon")).toDouble(), 0, 'f', 4)) &&
                     detailText.contains(QStringLiteral("길이|00:02")) && detailText.contains(QStringLiteral("삭제 예정|")),
                 qPrintable(detailText));
        _grabIfCapturing(QStringLiteral("pr_2_received"));

        // 재생: the copy plays.
        QVERIFY2(clickButton(QStringLiteral("recordingsPlay")), "Could not tap 재생");
        QQuickItem *const playerTime = findVisibleItem(_rootItem, QStringLiteral("recordingsPlayerTime"), 1000);
        QVERIFY2(playerTime, "No player time");
        QObject *const player = page->findChild<QObject *>(QStringLiteral("recordingsMediaPlayer"));
        QVERIFY2(player, "No media player");
        // Loaded and playing the copy; the offscreen software renderer draws no video frame, so the
        // picture itself is not checked.
        QVERIFY2(waitWithMainQueue([&] { return player->property("duration").toLongLong() > 1500; }, TestTimeout::mediumMs()),
                 qPrintable(QStringLiteral("The player did not load the copy: status %1, error %2 %3")
                                .arg(player->property("mediaStatus").toInt()).arg(player->property("error").toInt())
                                .arg(player->property("errorString").toString())));
        // And running: the page's time moves on.
        QVERIFY2(waitWithMainQueue([&] { return playerTime->property("text").toString() == QStringLiteral("00:01 / 00:02"); }, TestTimeout::mediumMs()),
                 qPrintable(QStringLiteral("The copy did not play: %1").arg(playerTime->property("text").toString())));
        QCOMPARE(player->property("error").toInt(), 0);
        QCOMPARE(player->property("source").toUrl(), QUrl::fromLocalFile(QDir(SiyiRecordings::storeDirectory()).filePath(expected)));
        QCOMPARE(textOf(QStringLiteral("recordingsPlay")), QStringLiteral("일시 정지"));
        _grabIfCapturing(QStringLiteral("pr_3_playing"), 0);
        // To its end, and 재생 again.
        QVERIFY(waitWithMainQueue([&] { return player->property("playbackState").toInt() == 0; }, TestTimeout::mediumMs()));
        QVERIFY_TRUE_WAIT(textOf(QStringLiteral("recordingsPlay")) == QStringLiteral("재생"), 3000);

        // A stalled one shows how far it got, from the pod's length for it.
        QVERIFY2(clickButton(QStringLiteral("recordingsFetch_1")), "Could not tap 태블릿으로 받기 on the second row");
        QVERIFY_TRUE_WAIT(textOf(QStringLiteral("recordingsPercent_1")) == QStringLiteral("50%"), TestTimeout::mediumMs());
        QVERIFY2(rowText(1).contains(QStringLiteral("받는 중")), qPrintable(rowText(1)));
        _grabIfCapturing(QStringLiteral("pr_4_receiving"), 500);
        recordings->cancel();
        QVERIFY_TRUE_WAIT(rowText(1).contains(QStringLiteral("받기 전")), 3000);

        // Nothing logged for it: the row says so, and the pane has the pod's time and no position.
        QVERIFY2(clickButton(QStringLiteral("recordingsRow_1")), "Could not select the second row");
        QVERIFY_TRUE_WAIT(textOf(QStringLiteral("recordingsDetailName")) == QStringLiteral("VID_0030.MP4"), 3000);
        QVERIFY2(joinedTexts(detail).contains(QStringLiteral("촬영 일시|%1|촬영 위치|없음").arg(start.addSecs(-3600).toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))),
                 qPrintable(joinedTexts(detail)));
        _grabIfCapturing(QStringLiteral("pr_4b_unlogged"));

        // As the operator, 내보내기 wants the PIN; a wrong one stays out. The page asks DeveloperPin,
        // whose shipped PIN is set per build, so the test gives it one it knows.
        PinGate knownPin(QStringLiteral("PoliceDrone/DeveloperPin"), QStringLiteral("0"), nullptr);
        QVERIFY(knownPin.changePin(QStringLiteral("0"), QStringLiteral("704183")));
        QVERIFY2(clickButton(QStringLiteral("recordingsRow_0")), "Could not select the received row");
        QVERIFY2(clickButton(QStringLiteral("recordingsExport")), "Could not tap 내보내기");
        QVERIFY2(waitForDialog(QStringLiteral("관리자 확인")), "내보내기 asked for no PIN");
        QQuickItem *pinField = findVisibleItem(_rootItem, QStringLiteral("recordingsPinField"), 1000);
        QVERIFY2(pinField, "No PIN field");
        pinField->setProperty("text", QStringLiteral("000000"));
        _grabIfCapturing(QStringLiteral("pr_5_pin"), 500);
        QVERIFY(acceptDialog());
        QTest::qWait(500);
        QVERIFY2(dialogVisible(QStringLiteral("관리자 확인")), "A wrong PIN closed the dialog");
        QVERIFY(!QGCCorePlugin::instance()->showAdvancedUI());
        pinField = findVisibleItem(_rootItem, QStringLiteral("recordingsPinField"), 1000);
        QVERIFY2(pinField, "The PIN field went");
        pinField->setProperty("text", QStringLiteral("704183"));
        QVERIFY(acceptDialog());
        QVERIFY_TRUE_WAIT(!dialogVisible(QStringLiteral("관리자 확인")), 3000);
        QVERIFY(QGCCorePlugin::instance()->showAdvancedUI());
        const QString exported = QDir(SettingsManager::instance()->appSettings()->videoSavePath()).filePath(expected);
        QQuickItem *const message = findVisibleItem(_rootItem, QStringLiteral("recordingsMessage"), 3000);
        QVERIFY2(message, "The export said nothing");
        QCOMPARE(message->property("text").toString(), QStringLiteral("내보냈습니다: %1").arg(exported));
        QVERIFY(QFile::exists(exported));
        _grabIfCapturing(QStringLiteral("pr_6_exported"));
        QVERIFY(QFile::remove(exported));

        // 삭제, now the administrator: asked to confirm, then the copy goes and the pod's file stays.
        QVERIFY2(clickButton(QStringLiteral("recordingsDelete")), "Could not tap 삭제");
        QVERIFY2(waitForDialog(QStringLiteral("삭제")), "삭제 asked nothing");
        _grabIfCapturing(QStringLiteral("pr_7_delete_confirm"), 500);
        QVERIFY(acceptDialog());
        QVERIFY_TRUE_WAIT(!QFile::exists(QDir(SiyiRecordings::storeDirectory()).filePath(expected)), 3000);
        QVERIFY_TRUE_WAIT(rowText(0).contains(QStringLiteral("받기 전")), 3000);
        QVERIFY_TRUE_WAIT(summary->property("text").toString() == QStringLiteral("카메라에 녹화 파일 3개, 태블릿에 받은 파일 0개, 최신순"), 3000);

        // Back to the operator, and 접근 사용자 관리자: the list goes behind the PIN.
        QVERIFY(QGCCorePlugin::instance()->setProperty("showAdvancedUI", false));
        podSettings->recordingAdminOnly()->setRawValue(true);
        QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("recordingsLocked"), 3000), "관리자 only did not lock the records");
        QVERIFY2(!findVisibleItem(_rootItem, QStringLiteral("recordingsList"), 0), "The list stayed in view");
        _grabIfCapturing(QStringLiteral("pr_8_admin_only"));
        podSettings->recordingAdminOnly()->setRawValue(false);

        // The settings' 녹화 group, its example following the form.
        QVERIFY2(QMetaObject::invokeMethod(_window, "showToolSelectDialog"), "Could not open the menu");
        QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("toolbar_viewSettings"), 3000), "The menu has no 환경설정");
        QVERIFY(clickButton(QStringLiteral("toolbar_viewSettings")));
        QQuickItem *const videoButton = findVisibleItem(_rootItem, QStringLiteral("settingsButton_Video"), 3000);
        QVERIFY2(videoButton, "No 영상 settings button");
        scrollIntoView(videoButton, QStringLiteral("settings_buttonList"));
        QVERIFY(clickButton(QStringLiteral("settingsButton_Video")));
        QQuickItem *const nameCombo = findVisibleItem(_rootItem, QStringLiteral("settingsComboBox_recordingFileName"), 3000);
        QVERIFY2(nameCombo, "No 녹화 파일 이름 on the video settings");
        QCOMPARE(nameCombo->property("currentText").toString(), QStringLiteral("연월일시분초_위도_경도"));
        QQuickItem *const videoPage = findVisibleItem(_rootItem, QStringLiteral("settingsPage_Video"), 1000);
        QVERIFY2(videoPage, "No video settings page");
        QVERIFY2(joinedTexts(videoPage).contains(QStringLiteral("녹화|녹화 파일 이름|받을 때 붙는 이름, 예: 20260928143015123_37.5665_126.9780.mp4")),
                 qPrintable(joinedTexts(videoPage)));
        _grabIfCapturing(QStringLiteral("pr_9_settings"));
        podSettings->recordingFileName()->setRawValue(0);
        QVERIFY_TRUE_WAIT(nameCombo->property("currentText").toString() == QStringLiteral("연월일시분초"), 3000);
        QVERIFY2(joinedTexts(videoPage).contains(QStringLiteral("받을 때 붙는 이름, 예: 20260928143015123.mp4")), qPrintable(joinedTexts(videoPage)));
        _grabIfCapturing(QStringLiteral("pr_10_settings_time_only"));
        podSettings->recordingFileName()->setRawValue(1);

        pod->stop();
    });
}
