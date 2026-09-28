#include "PoliceFlightRecordsUITest.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "AppSettings.h"
#include "FlightRecords.h"
#include "MockLink.h"
#include "MockLinkFTP.h"
#include "SettingsManager.h"
#include "TakeoffCounter.h"
#include "Vehicle.h"

UT_REGISTER_TEST(PoliceFlightRecordsUITest, TestLabel::Integration)

namespace {

const QString kBanner       = QStringLiteral("policeStatusBanner");
const QString kDrawerLoader = QStringLiteral("indicatorDrawerLoader");
const QString kLink         = QStringLiteral("policeFlightRecordsLink");
const QString kPage         = QStringLiteral("policeFlightRecordsPage");
const QString kTable        = QStringLiteral("flightRecordsTable");
const QString kSummary      = QStringLiteral("flightRecordsSummary");
const QString kMessage      = QStringLiteral("flightRecordsMessage");
const QString kLogState     = QStringLiteral("flightRecordsLogState");

constexpr int kSettleMs = 1500;

/// Long enough in the air that the duration rounds to whole seconds, and the two flights'
/// takeoffs, which name their telemetry logs to the second, stay apart.
constexpr int kFlightMs = 2500;

/// The tablet's 1920x1200 under QT_SCALE_FACTOR=2.5, as the other police UI tests size it.
constexpr int kLayoutWidth  = 768;
constexpr int kLayoutHeight = 480;

/// Big enough that the fetch takes seconds at the mock's one LOG_DATA every 2 ms, so the page is
/// seen on its way.
constexpr int kVehicleLogBytes = 180000;

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

/// The counter is started by QGCApplication::_initForNormalAppBoot, which the test harness does
/// not run. Once per process: a second init() would count every takeoff twice.
void startTakeoffCounter()
{
    static bool started = false;
    if (!started) {
        TakeoffCounter::instance()->init();
        started = true;
    }
}

}  // namespace

void PoliceFlightRecordsUITest::init()
{
    QVERIFY(QDir(TakeoffCounter::recordsDirectory()).removeRecursively());
    QmlUITestBase::init();
}

void PoliceFlightRecordsUITest::_ignorePreexistingQmlWarnings()
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
    // The flying transition creates QGCPressure, which warns on hosts without a backend.
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));
}

void PoliceFlightRecordsUITest::_grabIfCapturing(const QString &name, int settleMs)
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

void PoliceFlightRecordsUITest::_testRecordsPage()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        startTakeoffCounter();
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        // Up to 1.5 m and back to 0 on the pad, as PoliceTopBarUITest flies the mock.
        const auto fly = [&](double metres) {
            mockLink->setArmed(true);
            QVERIFY_TRUE_WAIT(vehicle->armed(), TestTimeout::mediumMs());
            vehicle->sendMavCommand(vehicle->defaultComponentId(), MAV_CMD_NAV_TAKEOFF, false, 0, 0, 0, 0, 0, 0, 1.5f);
            QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::mediumMs());
            vehicle->updateFlightDistance(metres);
            QTest::qWait(kFlightMs);
            vehicle->sendMavCommand(vehicle->defaultComponentId(), MAV_CMD_NAV_TAKEOFF, false, 0, 0, 0, 0, 0, 0, 0.0f);
            QVERIFY_TRUE_WAIT(!vehicle->flying(), TestTimeout::mediumMs());
            mockLink->setArmed(false);
            QVERIFY_TRUE_WAIT(!vehicle->armed(), TestTimeout::mediumMs());
        };
        fly(850.0);
        if (QTest::currentTestFailed()) {
            return;
        }
        // On the pad a moment between the sorties, so the first one's log has ended a clear second
        // before the second takes off.
        QTest::qWait(kSettleMs);
        fly(1234.5);
        if (QTest::currentTestFailed()) {
            return;
        }

        // The aircraft's own logs, each stamped with its last write as PX4 and ArduPilot report it:
        // at its flight's landing. The newest flight's fetch has to pick the second.
        MockLinkFTP *const ftp = mockLink->mockLinkFTP();
        QVERIFY(ftp);
        const QVariantList flown = TakeoffCounter::instance()->flights();
        QCOMPARE(flown.size(), 2);
        const auto landed = [&](int index) {
            return static_cast<uint32_t>(QDateTime::fromString(flown.at(index).toMap().value("landing").toString(), Qt::ISODate).toSecsSinceEpoch());
        };
        ftp->setLogFiles({ { QStringLiteral("log_0.ulg"), 3000, landed(1) },
                           { QStringLiteral("log_1.ulg"), kVehicleLogBytes, landed(0) } });

        // A second airframe's flight forty days back: outside the thirty the page opens on.
        const QDateTime oldTakeoff = QDateTime::currentDateTime().addDays(-40);
        QVERIFY(TakeoffCounter::writeFlights(QStringLiteral("sysid-250"), {
            QVariantMap{ { "takeoff", oldTakeoff.toString(Qt::ISODate) }, { "seconds", 600 }, { "metres", 2000.0 },
                         { "vehicle", QStringLiteral("250호기") } },
        }));

        // 전체 기록, under the drawer's list.
        QVERIFY2(clickButton(kBanner), "Could not tap the status banner");
        QVERIFY2(findVisibleItem(_rootItem, kDrawerLoader, 5000), "The banner opened no drawer");
        QQuickItem *const link = findVisibleItem(_rootItem, kLink, 3000);
        QVERIFY2(link, "The status drawer has no 전체 기록 row");
        QVERIFY2(joinedTexts(link).contains(QStringLiteral("전체 기록")), qPrintable(joinedTexts(link)));
        QQuickItem *const list = findVisibleItem(_rootItem, QStringLiteral("policeFlightList"), 3000);
        QVERIFY2(list, "The drawer lost its flight list");
        QCOMPARE(list->property("count").toInt(), 2);
        QVERIFY2(link->mapToScene(QPointF(0, 0)).y() >= list->mapToScene(QPointF(0, list->height())).y(),
                 "전체 기록 is not under the list");
        _grabIfCapturing(QStringLiteral("fr_0_drawer_link"));

        QVERIFY2(clickButton(kLink), "Could not tap 전체 기록");
        QQuickItem *const page = findVisibleItem(_rootItem, kPage, 5000);
        QVERIFY2(page, "전체 기록 opened no records page");
        QVERIFY2(waitForCondition([&] { return findVisibleItem(_rootItem, kDrawerLoader, 0) == nullptr; }, 3000,
                                  QStringLiteral("status drawer closed")), "The drawer stayed over the page");
        QQuickItem *const toolDrawer = findVisibleItem(_rootItem, QStringLiteral("mainView_toolDrawer"), 1000);
        QVERIFY2(toolDrawer, "The page is not in the tool drawer");
        QCOMPARE(toolDrawer->property("toolTitle").toString(), QStringLiteral("비행 기록"));

        QQuickItem *const table = findVisibleItem(_rootItem, kTable, 3000);
        QVERIFY2(table, "The page has no table");
        QQuickItem *const summary = findVisibleItem(_rootItem, kSummary, 1000);
        QVERIFY2(summary, "The page has no summary line");
        QQuickItem *const fromField = findVisibleItem(_rootItem, QStringLiteral("flightRecordsFrom"), 1000);
        QQuickItem *const toField = findVisibleItem(_rootItem, QStringLiteral("flightRecordsTo"), 1000);
        QVERIFY2(fromField && toField, "The page has no date fields");

        // The last thirty days, this airframe's two flights, newest first.
        const QDate today = QDate::currentDate();
        QCOMPARE(toField->property("text").toString(), today.toString(Qt::ISODate));
        QCOMPARE(fromField->property("text").toString(), today.addDays(-29).toString(Qt::ISODate));
        QVERIFY_TRUE_WAIT(table->property("count").toInt() == 2, 3000);
        const QString summaryText = summary->property("text").toString();
        QVERIFY2(summaryText.contains(QStringLiteral("2건")) && summaryText.contains(QStringLiteral("총 비행 시간")) &&
                     summaryText.contains(QStringLiteral("총 거리 2084.5 m")),
                 qPrintable(summaryText));

        QQuickItem *row = nullptr;
        QVERIFY_TRUE_WAIT(QMetaObject::invokeMethod(table, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, 0)) && row,
                          3000);
        QVERIFY2(row, "The newest row was not created");
        const QString rowText = joinedTexts(row);
        QVERIFY2(QRegularExpression(QStringLiteral("^\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\|\\d\\d:\\d\\d:\\d\\d\\|00:00:0\\d\\|"
                                                   "1234\\.5 m\\|\\d m\\|\\d+호기\\|\\d{8}_\\d{6}\\.tlog$")).match(rowText).hasMatch(),
                 qPrintable(QStringLiteral("The newest row reads %1").arg(rowText)));
        _grabIfCapturing(QStringLiteral("fr_1_page"));

        // 조회 over sixty days takes in the other airframe's flight, last.
        const auto query = [&](const QString &from, const QString &to) {
            fromField->setProperty("text", from);
            toField->setProperty("text", to);
            QVERIFY2(clickButton(QStringLiteral("flightRecordsQuery")), "Could not tap 조회");
        };
        query(today.addDays(-59).toString(Qt::ISODate), today.toString(Qt::ISODate));
        QVERIFY_TRUE_WAIT(table->property("count").toInt() == 3, 3000);
        // A delegate is made on the view's next polish, after the count.
        QQuickItem *oldRow = nullptr;
        QVERIFY_TRUE_WAIT(QMetaObject::invokeMethod(table, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, oldRow), Q_ARG(int, 2)) && oldRow,
                          3000);
        QVERIFY2(oldRow, "The oldest row was not created");
        QVERIFY2(joinedTexts(oldRow).contains(QStringLiteral("250호기")), qPrintable(joinedTexts(oldRow)));
        _grabIfCapturing(QStringLiteral("fr_2_sixty_days"));

        // Its day alone, then back to the thirty.
        query(oldTakeoff.date().toString(Qt::ISODate), oldTakeoff.date().toString(Qt::ISODate));
        QVERIFY_TRUE_WAIT(table->property("count").toInt() == 1, 3000);
        query(today.addDays(-29).toString(Qt::ISODate), today.toString(Qt::ISODate));
        QVERIFY_TRUE_WAIT(table->property("count").toInt() == 2, 3000);

        // A tick counts in the summary and is what the export takes.
        QVERIFY2(clickButton(QStringLiteral("flightRecordsCheck_0")), "Could not tick the newest row");
        QVERIFY_TRUE_WAIT(summary->property("text").toString().contains(QStringLiteral("1건 선택")), 3000);

        const QDir exportRoot(FlightRecords::instance()->exportDirectory());
        const QStringList before = exportRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY2(clickButton(QStringLiteral("flightRecordsExport")), "Could not tap 선택 내보내기");
        QQuickItem *const message = findVisibleItem(_rootItem, kMessage, 3000);
        QVERIFY2(message, "The export said nothing");
        QVERIFY2(message->property("text").toString().startsWith(QStringLiteral("1건을 내보냈습니다")),
                 qPrintable(message->property("text").toString()));
        QStringList added = exportRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : before) {
            added.removeAll(name);
        }
        QCOMPARE(added.size(), 1);
        const QDir exported(exportRoot.filePath(added.first()));
        const QVariantMap newest = TakeoffCounter::instance()->flights().first().toMap();
        const QString tlogName = newest.value("tlog").toString();
        QVERIFY2(!tlogName.isEmpty(), "The newest flight has no telemetry log");
        QCOMPARE(exported.entryList(QDir::Files, QDir::Name), QStringList({ tlogName, QStringLiteral("flights.csv") }));
        QFile csv(exported.filePath(QStringLiteral("flights.csv")));
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(csv.readAll()).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        QVERIFY2(lines.at(1).contains(newest.value("takeoff").toString().replace(QLatin1Char('T'), QLatin1Char(' '))) &&
                     lines.at(1).contains(QStringLiteral(",1234.5,")) && lines.at(1).endsWith(tlogName + QLatin1Char(',')),
                 qPrintable(lines.at(1)));
        _grabIfCapturing(QStringLiteral("fr_3_exported"));

        // USB로 저장 with no stick in. Only checked when this machine has none mounted: a real one
        // would be written to.
        if (FlightRecords::instance()->usbDirectory().isEmpty()) {
            QVERIFY2(clickButton(QStringLiteral("flightRecordsUsb")), "Could not tap USB로 저장");
            QVERIFY_TRUE_WAIT(message->property("text").toString() == QStringLiteral("USB 저장장치가 연결되어 있지 않습니다"), 3000);
            _grabIfCapturing(QStringLiteral("fr_4_no_usb"));
        }

        // The newest flight's own log: 받기 전, 받는 중, 받음.
        QQuickItem *const logState = findVisibleItem(_rootItem, kLogState, 1000);
        QVERIFY2(logState, "The detail pane has no log state");
        QVERIFY2(joinedTexts(logState).contains(QStringLiteral("받기 전")), qPrintable(joinedTexts(logState)));
        _grabIfCapturing(QStringLiteral("fr_5_log_before"));

        QVERIFY2(clickButton(QStringLiteral("flightRecordsDownload")), "Could not tap 기체 로그 받기");
        // Seen on its way, far enough along for the bar to read.
        const QRegularExpression receiving(QStringLiteral("받는 중 (\\d+)%"));
        QVERIFY_TRUE_WAIT(receiving.match(joinedTexts(logState)).captured(1).toInt() >= 30, TestTimeout::longMs());
        QVERIFY2(receiving.match(joinedTexts(logState)).captured(1).toInt() < 100, qPrintable(joinedTexts(logState)));
        _grabIfCapturing(QStringLiteral("fr_6_log_receiving"), 0);
        QQuickItem *const download = findVisibleItem(_rootItem, QStringLiteral("flightRecordsDownload"), 1000);
        QVERIFY2(download, "기체 로그 받기 left the pane while the log came in");
        QVERIFY2(!download->isEnabled(), "기체 로그 받기 stayed live during the fetch");

        QVERIFY_TRUE_WAIT(joinedTexts(logState).contains(QStringLiteral("받음")), 30000);
        QVERIFY2(QRegularExpression(QStringLiteral("\\|log_1_[^|]+\\.(ulg|px4log)$")).match(joinedTexts(logState)).hasMatch(),
                 qPrintable(joinedTexts(logState)));
        QVERIFY2(!findVisibleItem(_rootItem, QStringLiteral("flightRecordsDownload"), 0), "기체 로그 받기 is still offered once received");
        _grabIfCapturing(QStringLiteral("fr_7_log_received"));
    });
}
