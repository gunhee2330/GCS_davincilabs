#include "SiyiRecordingsTest.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "AppSettings.h"
#include "FakePodMediaServer.h"
#include "Fact.h"
#include "SettingsManager.h"
#include "SiyiCameraSettings.h"
#include "SiyiRecordings.h"

namespace {

qint64 localMs(int y, int mo, int d, int h, int mi, int s, int ms = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s, ms)).toMSecsSinceEpoch();
}

QByteArray sampleVideo()
{
    QFile file(QStringLiteral(":/unittest/pod_sample.mp4"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QVariantMap fileWithKey(const SiyiRecordings &recordings, const QString &key)
{
    for (const QVariant &file : recordings.files()) {
        if (file.toMap().value(QStringLiteral("key")).toString() == key) {
            return file.toMap();
        }
    }
    return {};
}

SiyiCameraSettings *podSettings()
{
    return SettingsManager::instance()->siyiCameraSettings();
}

/// Lists the pod at loopback, \a recordings already pointed at the fake server's port.
bool listFrom(SiyiRecordings &recordings)
{
    podSettings()->ipAddress()->setRawValue(QStringLiteral("127.0.0.1"));
    recordings.refresh();
    return QTest::qWaitFor([&] { return !recordings.listing(); }, TestTimeout::mediumMs());
}

}  // namespace

void SiyiRecordingsTest::init()
{
    UnitTest::init();
    QVERIFY(QDir(SiyiRecordings::storeDirectory()).removeRecursively());
    podSettings()->recordingFileName()->setRawValue(1);
    podSettings()->recordingAutoDeleteDays()->setRawValue(30);
}

void SiyiRecordingsTest::_namesAndMatching_test()
{
    const qint64 start = localMs(2026, 9, 28, 14, 30, 15, 123);
    QCOMPARE(SiyiRecordings::rfpName(start, false, 0, 0), QStringLiteral("20260928143015123.mp4"));
    QCOMPARE(SiyiRecordings::rfpName(start, true, 37.56654, 126.97801), QStringLiteral("20260928143015123_37.5665_126.9780.mp4"));
    QCOMPARE(SiyiRecordings::rfpName(start, true, -33.8688, 151.2093), QStringLiteral("20260928143015123_-33.8688_151.2093.mp4"));

    QCOMPARE(SiyiRecordings::nameTime(QStringLiteral("VID_0031.MP4")), 0);
    QCOMPARE(SiyiRecordings::nameTime(QStringLiteral("REC_20260928_143015.mp4")), localMs(2026, 9, 28, 14, 30, 15));
    QCOMPARE(SiyiRecordings::nameTime(QStringLiteral("2026-09-28 14-30-15.mp4")), localMs(2026, 9, 28, 14, 30, 15));
    QCOMPARE(SiyiRecordings::nameTime(QStringLiteral("20260928143015123_37.5665_126.9780.mp4")), start);
    QCOMPARE(SiyiRecordings::nameTime(QStringLiteral("20261399999999.mp4")), 0);

    const qint64 t = localMs(2026, 9, 28, 10, 0, 0);
    const QJsonArray events{
        QJsonObject{ { "ms", t }, { "lat", 37.5 }, { "lon", 127.0 } },
        QJsonObject{ { "ms", t + 6 * 60 * 1000 } },
        QJsonObject{ { "ms", t + 7 * 60 * 1000 + 20 * 1000 } },
    };
    // A start the pod names: the nearest logged start, either side, within the slack.
    QCOMPARE(SiyiRecordings::matchEvent(events, t - 4000, false).value("ms").toInteger(), t);
    QCOMPARE(SiyiRecordings::matchEvent(events, t + 6 * 60 * 1000 + 5000, false).value("ms").toInteger(), t + 6 * 60 * 1000);
    QVERIFY(SiyiRecordings::matchEvent(events, t + 3 * 60 * 1000, false).isEmpty());
    // A last write: the last start before it, not the nearest, which may be the next recording's.
    QCOMPARE(SiyiRecordings::matchEvent(events, t + 5 * 60 * 1000 + 55 * 1000, true).value("ms").toInteger(), t);
    QCOMPARE(SiyiRecordings::matchEvent(events, t + 60 * 60 * 1000, true).value("ms").toInteger(), t + 7 * 60 * 1000 + 20 * 1000);
    QVERIFY(SiyiRecordings::matchEvent(events, t - 60 * 1000, true).isEmpty());
    QVERIFY(SiyiRecordings::matchEvent(events, t + 5 * 60 * 60 * 1000, true).isEmpty());
}

void SiyiRecordingsTest::_recordingLog_test()
{
    {
        SiyiRecordings recordings(nullptr);
        recordings._podState(true, true);    // already recording when first heard: not a start
        QCOMPARE(recordings.events().size(), 0);
        recordings._podState(true, false);
        recordings._podState(true, true);    // a start
        QCOMPARE(recordings.events().size(), 1);
        recordings._podState(true, true);
        recordings._podState(false, false);  // link lost mid recording
        recordings._podState(true, true);    // back, still recording: not a start
        QCOMPARE(recordings.events().size(), 1);
        const qint64 ms = recordings.events().first().toObject().value("ms").toInteger();
        QVERIFY(qAbs(ms - QDateTime::currentMSecsSinceEpoch()) < 5000);
        // No vehicle, no position
        QVERIFY(!recordings.events().first().toObject().contains("lat"));

        recordings.logRecordingStart(1000, QGeoCoordinate(37.5, 127.0));
        QCOMPARE(recordings.events().last().toObject().value("lat").toDouble(), 37.5);
    }
    SiyiRecordings reopened(nullptr);
    QCOMPARE(reopened.events().size(), 2);
}

void SiyiRecordingsTest::_listsAndDownloads_test()
{
    const QByteArray video = sampleVideo();
    QVERIFY(!video.isEmpty());

    const QDateTime named(QDate(2026, 9, 28), QTime(14, 30, 15));
    const QDateTime unnamedEnd(QDate(2026, 9, 27), QTime(16, 20, 0));
    FakePodMediaServer server;
    server.files = {
        { QStringLiteral("20260928"), QStringLiteral("REC_20260928_143015.mp4"), video, named.addSecs(135), false },
        { QStringLiteral("20260927"), QStringLiteral("VID_0029.MP4"), video + QByteArray(1000, 'x'), unnamedEnd, false },
    };
    QVERIFY(server.listen());

    SiyiRecordings recordings(nullptr);
    // The first file's start was logged 2 s after the pod's, with the aircraft over Seoul; nothing for the second.
    recordings.logRecordingStart(named.toMSecsSinceEpoch() + 2123, QGeoCoordinate(37.56654, 126.97801));

    recordings._mediaPort = server.port();
    QVERIFY(listFrom(recordings));
    QCOMPARE(recordings.listError(), QString());
    QCOMPARE(recordings.files().size(), 2);

    const QString namedKey = QStringLiteral("20260928/REC_20260928_143015.mp4");
    const QString unnamedKey = QStringLiteral("20260927/VID_0029.MP4");
    QVariantMap first = fileWithKey(recordings, namedKey);
    QVERIFY(first.value("onPod").toBool());
    QVERIFY(!first.value("received").toBool());
    QCOMPARE(first.value("timeSource").toString(), QStringLiteral("log"));
    QCOMPARE(first.value("startMs").toLongLong(), named.toMSecsSinceEpoch() + 2123);
    QVERIFY(first.value("hasPosition").toBool());
    // Newest first
    QCOMPARE(recordings.files().first().toMap().value("key").toString(), namedKey);

    // No time in the name: its last write, from a HEAD, which also gives its size
    QVariantMap second = fileWithKey(recordings, unnamedKey);
    QCOMPARE(second.value("podMs").toLongLong(), unnamedEnd.toMSecsSinceEpoch());
    QCOMPARE(second.value("timeSource").toString(), QStringLiteral("pod"));
    QCOMPARE(second.value("size").toLongLong(), video.size() + 1000);
    QCOMPARE(server.fileGets, 0);

    // Time and position
    QSignalSpy progress(&recordings, &SiyiRecordings::downloadChanged);
    recordings.fetch(namedKey);
    QCOMPARE(recordings.download().value("key").toString(), namedKey);
    QVERIFY(QTest::qWaitFor([&] { return fileWithKey(recordings, namedKey).value("received").toBool(); }, TestTimeout::mediumMs()));
    QVERIFY(progress.count() >= 2);
    QCOMPARE(recordings.download().value("key").toString(), QString());
    QCOMPARE(recordings.download().value("percent").toInt(), 100);
    first = fileWithKey(recordings, namedKey);
    const QString expected = SiyiRecordings::rfpName(named.toMSecsSinceEpoch() + 2123, true, 37.56654, 126.97801);
    QCOMPARE(first.value("localName").toString(), expected);
    QVERIFY(expected.endsWith(QStringLiteral("_37.5665_126.9780.mp4")));
    QCOMPARE(readFile(QDir(SiyiRecordings::storeDirectory()).filePath(expected)), video);
    QCOMPARE(first.value("durationS").toInt(), 2);
    QCOMPARE(first.value("size").toLongLong(), video.size());
    QVERIFY(qAbs(first.value("deleteAtMs").toLongLong() - (first.value("receivedMs").toLongLong() + 30LL * 86400000)) < 1000);
    QVERIFY(!QFile::exists(QDir(SiyiRecordings::storeDirectory()).filePath(QStringLiteral(".download.part"))));

    // Time alone, by the setting; nothing logged for this one, so the pod's time and no position
    podSettings()->recordingFileName()->setRawValue(0);
    recordings.fetchAll();
    QVERIFY(QTest::qWaitFor([&] { return fileWithKey(recordings, unnamedKey).value("received").toBool(); }, TestTimeout::mediumMs()));
    second = fileWithKey(recordings, unnamedKey);
    QCOMPARE(second.value("localName").toString(), SiyiRecordings::rfpName(unnamedEnd.toMSecsSinceEpoch(), false, 0, 0));
    QCOMPARE(second.value("timeSource").toString(), QStringLiteral("pod"));
    QVERIFY(!second.value("hasPosition").toBool());
    QCOMPARE(server.fileGets, 2);

    // Both there after a restart, with the pod out of reach
    {
        SiyiRecordings reopened(nullptr);
        QCOMPARE(reopened.files().size(), 2);
        QVERIFY(!fileWithKey(reopened, namedKey).value("onPod").toBool());
        QCOMPARE(fileWithKey(reopened, namedKey).value("localName").toString(), expected);
        QVERIFY(reopened.playbackUrl(namedKey).isLocalFile());
    }

    // Export into the video folder under the same name
    const QString exported = recordings.exportCopy(namedKey);
    QCOMPARE(exported, QDir(SettingsManager::instance()->appSettings()->videoSavePath()).filePath(expected));
    QCOMPARE(readFile(exported), video);
    QCOMPARE(recordings.exportCopy(namedKey), exported);
    // Another file under that name is kept, and the copy goes beside it
    {
        QFile other(exported);
        QVERIFY(other.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(other.write("another file") > 0);
    }
    const QString beside = recordings.exportCopy(namedKey);
    QVERIFY2(beside.endsWith(QStringLiteral("_2.mp4")), qPrintable(beside));
    QCOMPARE(readFile(beside), video);
    QCOMPARE(readFile(exported), QByteArray("another file"));
    QCOMPARE(recordings.exportCopy(namedKey), beside);
    QVERIFY(QFile::remove(beside));
    QVERIFY(QFile::remove(exported));

    // Delete: the copy goes, the pod's file stays listed
    QVERIFY(recordings.remove(namedKey));
    QVERIFY(!QFile::exists(QDir(SiyiRecordings::storeDirectory()).filePath(expected)));
    first = fileWithKey(recordings, namedKey);
    QVERIFY(first.value("onPod").toBool());
    QVERIFY(!first.value("received").toBool());
    QVERIFY(recordings.playbackUrl(namedKey).isEmpty());
}

void SiyiRecordingsTest::_cancel_test()
{
    FakePodMediaServer server;
    server.files = { { QStringLiteral("20260928"), QStringLiteral("VID_0030.MP4"), QByteArray(200000, 'v'), QDateTime::currentDateTime(), true } };
    QVERIFY(server.listen());

    SiyiRecordings recordings(nullptr);
    recordings._mediaPort = server.port();
    QVERIFY(listFrom(recordings));
    const QString key = QStringLiteral("20260928/VID_0030.MP4");
    QCOMPARE(fileWithKey(recordings, key).value("size").toLongLong(), 200000);
    recordings.fetch(key);
    // Half sent, no length with it: the HEAD's length gives the percent.
    QVERIFY(QTest::qWaitFor([&] { return recordings.download().value("percent").toInt() >= 50; }, TestTimeout::mediumMs()));
    QCOMPARE(recordings.download().value("key").toString(), key);

    recordings.cancel();
    QVERIFY(QTest::qWaitFor([&] { return recordings.download().value("key").toString().isEmpty(); }, TestTimeout::shortMs()));
    QCOMPARE(recordings.download().value("error").toString(), QString());
    QVERIFY(!fileWithKey(recordings, key).value("received").toBool());
    QCOMPARE(QDir(SiyiRecordings::storeDirectory()).entryList({ QStringLiteral("*.mp4"), QStringLiteral("*.part") }, QDir::Files | QDir::Hidden),
             QStringList());
}

void SiyiRecordingsTest::_autoDelete_test()
{
    const QDir store(SiyiRecordings::storeDirectory());
    QVERIFY(QDir().mkpath(store.path()));
    SiyiRecordings recordings(nullptr);
    const auto addCopy = [&](const QString &key, const QString &name, int ageDays) {
        QFile file(store.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("x") == 1);
        QVERIFY(file.flush());
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-ageDays), QFileDevice::FileModificationTime));
        file.close();
        recordings._index.insert(key, QJsonObject{ { "name", name }, { "podName", key }, { "startMs", 1 }, { "timeSource", "pod" } });
    };
    addCopy(QStringLiteral("d/old"), QStringLiteral("old.mp4"), 31);
    addCopy(QStringLiteral("d/new"), QStringLiteral("new.mp4"), 29);
    addCopy(QStringLiteral("d/older"), QStringLiteral("older.mp4"), 100);

    podSettings()->recordingAutoDeleteDays()->setRawValue(0);
    QCOMPARE(recordings.purge(), 0);
    QCOMPARE(recordings.files().size(), 3);

    podSettings()->recordingAutoDeleteDays()->setRawValue(30);
    QCOMPARE(recordings.purge(), 2);
    QVERIFY(!store.exists(QStringLiteral("old.mp4")));
    QVERIFY(!store.exists(QStringLiteral("older.mp4")));
    QVERIFY(store.exists(QStringLiteral("new.mp4")));
    QCOMPARE(recordings.files().size(), 1);
    // The index on disk follows
    QCOMPARE(SiyiRecordings(nullptr).files().size(), 1);

    podSettings()->recordingAutoDeleteDays()->setRawValue(7);
    QCOMPARE(recordings.purge(), 1);
    QCOMPARE(recordings.files().size(), 0);
}

UT_REGISTER_TEST(SiyiRecordingsTest, TestLabel::Unit)
