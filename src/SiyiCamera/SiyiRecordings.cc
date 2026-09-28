#include "SiyiRecordings.h"

#include <algorithm>
#include <limits>

#include <QtCore/QApplicationStatic>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrlQuery>
#include <QtCore/QtEndian>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtQml/QJSEngine>

#include "AppSettings.h"
#include "Fact.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "SiyiCameraController.h"
#include "SiyiCameraSettings.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(SiyiRecordingsLog, "SiyiCamera.SiyiRecordings")

namespace {

const QString kVideo = QStringLiteral("1");   ///< media_type of a video
/// Files asked for per folder, as siyi-download.py asks: the pod answers with what it has up to this.
const QString kListCount = QStringLiteral("9999");
constexpr int kApiTimeoutMs = 5000;
/// A download stalled this long, with nothing arriving, is given up.
constexpr int kDownloadStallMs = 15000;

/// How far the pod's clock and the tablet's may disagree for a file to be matched to a logged
/// recording start. A tuning knob: raise it if the pod's clock is set loosely and files go unmatched.
constexpr qint64 kClockSlackMs = 30 * 1000;
/// The longest a recording runs, which is how far a file's last write can be after its start.
constexpr qint64 kLongestRecordingMs = 3LL * 60 * 60 * 1000;
constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;

const QString kIndexFile  = QStringLiteral("index.json");
const QString kEventsFile = QStringLiteral("recording-events.json");
const QString kPartFile   = QStringLiteral(".download.part");

SiyiCameraSettings *settings()
{
    return SettingsManager::instance()->siyiCameraSettings();
}

QJsonDocument readJson(const QString &fileName)
{
    QFile file(QDir(SiyiRecordings::storeDirectory()).filePath(fileName));
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()) : QJsonDocument();
}

bool writeJson(const QString &fileName, const QJsonDocument &document)
{
    if (!QDir().mkpath(SiyiRecordings::storeDirectory())) {
        return false;
    }
    // Written aside and renamed over the old file, so a crash mid-write leaves the last good one.
    QSaveFile file(QDir(SiyiRecordings::storeDirectory()).filePath(fileName));
    return file.open(QIODevice::WriteOnly) && (file.write(document.toJson(QJsonDocument::Compact)) >= 0) && file.commit();
}

}  // namespace

Q_APPLICATION_STATIC(SiyiRecordings, _siyiRecordingsInstance, nullptr);

SiyiRecordings::SiyiRecordings(QObject *parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
    , _index(readJson(kIndexFile).object())
    , _events(readJson(kEventsFile).array())
{
    _purgeTimer.setInterval(std::chrono::hours(24));
    (void) connect(&_purgeTimer, &QTimer::timeout, this, &SiyiRecordings::purge);
}

SiyiRecordings::~SiyiRecordings()
{
    if (_reply) {
        (void) disconnect(_reply, nullptr, this, nullptr);
        _reply->abort();
    }
}

SiyiRecordings *SiyiRecordings::instance()
{
    return _siyiRecordingsInstance();
}

SiyiRecordings *SiyiRecordings::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    SiyiRecordings *const recordings = instance();
    QJSEngine::setObjectOwnership(recordings, QJSEngine::CppOwnership);
    return recordings;
}

void SiyiRecordings::init()
{
    if (_initialized) {
        return;
    }
    _initialized = true;

    SiyiCameraController *const pod = SiyiCameraController::instance();
    (void) connect(pod, &SiyiCameraController::connectedChanged, this, [this, pod]() {
        if (!pod->connected()) {
            _podState(false, false);
        }
    });
    // Recording is read off the pod's config report, polled once a second, so a logged start is up
    // to a second after the pod's own.
    (void) connect(pod, &SiyiCameraController::configChanged, this, [this, pod]() {
        _podState(pod->connected(), pod->recording());
    });
    (void) connect(settings()->recordingAutoDeleteDays(), &Fact::rawValueChanged, this, &SiyiRecordings::purge);

    _purgeTimer.start();
    (void) purge();
}

void SiyiRecordings::_podState(bool connected, bool recording)
{
    if (!connected) {
        _recordingKnown = false;
        return;
    }
    if (!_recordingKnown) {
        _recordingKnown = true;
        _podRecording = recording;
        return;
    }
    if (recording && !_podRecording) {
        Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
        logRecordingStart(QDateTime::currentMSecsSinceEpoch(), vehicle ? vehicle->coordinate() : QGeoCoordinate());
    }
    _podRecording = recording;
}

void SiyiRecordings::logRecordingStart(qint64 ms, const QGeoCoordinate &position)
{
    QJsonObject event{ { QStringLiteral("ms"), ms } };
    if (position.isValid()) {
        event.insert(QStringLiteral("lat"), position.latitude());
        event.insert(QStringLiteral("lon"), position.longitude());
    }
    // ponytail: every start kept, a few dozen bytes each; prune past the oldest pod file if it ever grows large
    _events.append(event);
    if (!_saveEvents()) {
        qCWarning(SiyiRecordingsLog) << "could not write the recording log";
    }
    emit filesChanged();
}

QString SiyiRecordings::storeDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("PodRecordings"));
}

QString SiyiRecordings::rfpName(qint64 startMs, bool withPosition, double lat, double lon)
{
    const QString stamp = QDateTime::fromMSecsSinceEpoch(startMs).toString(QStringLiteral("yyyyMMddhhmmsszzz"));
    if (!withPosition) {
        return stamp + QStringLiteral(".mp4");
    }
    return QStringLiteral("%1_%2_%3.mp4").arg(stamp).arg(lat, 0, 'f', 4).arg(lon, 0, 'f', 4);
}

qint64 SiyiRecordings::nameTime(const QString &podName)
{
    static const QRegularExpression stamp(
        QStringLiteral("(\\d{4})[-_]?(\\d\\d)[-_]?(\\d\\d)[-_ T]?(\\d\\d)[-_:]?(\\d\\d)[-_:]?(\\d\\d)(\\d{3})?"));
    const QRegularExpressionMatch match = stamp.match(podName);
    if (!match.hasMatch()) {
        return 0;
    }
    // ponytail: the pod's names are read as the tablet's local time; if a pod names files in UTC, convert here
    const QDateTime time(QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt()),
                         QTime(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt(),
                               match.captured(7).toInt()));
    return (time.isValid() && (time.date().year() >= 2000)) ? time.toMSecsSinceEpoch() : 0;
}

QJsonObject SiyiRecordings::matchEvent(const QJsonArray &events, qint64 podMs, bool podMsIsEnd)
{
    QJsonObject best;
    qint64 bestGap = std::numeric_limits<qint64>::max();
    for (const QJsonValue &value : events) {
        const qint64 ms = value.toObject().value(QStringLiteral("ms")).toInteger();
        // A start the pod names: the logged start nearest it, on either side, as the clocks differ.
        // A file's last write: the last start before it. No slack past the end there, or a stop and
        // a restart moments apart would hand this file the next recording's start.
        const qint64 gap   = podMsIsEnd ? (podMs - ms) : qAbs(ms - podMs);
        const qint64 limit = podMsIsEnd ? kLongestRecordingMs : kClockSlackMs;
        if ((gap >= 0) && (gap <= limit) && (gap < bestGap)) {
            best = value.toObject();
            bestGap = gap;
        }
    }
    return best;
}

int SiyiRecordings::mp4DurationSeconds(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return -1;
    }
    // Box headers down to moov and its mvhd. A camera writes moov after the media, so each box on
    // the way is skipped by its size, never read.
    qint64 pos = 0;
    qint64 end = file.size();
    while (pos + 8 <= end) {
        if (!file.seek(pos)) {
            return -1;
        }
        const QByteArray header = file.read(16);
        if (header.size() < 8) {
            return -1;
        }
        qint64 size = qFromBigEndian<quint32>(header.constData());
        const QByteArray type = header.mid(4, 4);
        qint64 headerSize = 8;
        if (size == 1) {
            if (header.size() < 16) {
                return -1;
            }
            size = static_cast<qint64>(qFromBigEndian<quint64>(header.constData() + 8));
            headerSize = 16;
        } else if (size == 0) {
            size = end - pos;
        }
        if (size < headerSize) {
            return -1;
        }
        if (type == "moov") {
            end = pos + size;
            pos += headerSize;
            continue;
        }
        if (type == "mvhd") {
            if (!file.seek(pos + headerSize)) {
                return -1;
            }
            const QByteArray body = file.read(32);
            if (body.isEmpty()) {
                return -1;
            }
            // Version 1 widens the two times and the duration to 64 bits.
            const bool wide = body.at(0) == 1;
            const int timescaleAt = wide ? 20 : 12;
            if (body.size() < timescaleAt + (wide ? 12 : 8)) {
                return -1;
            }
            const quint32 timescale = qFromBigEndian<quint32>(body.constData() + timescaleAt);
            const quint64 duration = wide ? qFromBigEndian<quint64>(body.constData() + timescaleAt + 4)
                                          : qFromBigEndian<quint32>(body.constData() + timescaleAt + 4);
            return timescale ? static_cast<int>((duration + timescale / 2) / timescale) : -1;
        }
        pos += size;
    }
    return -1;
}

QJsonObject SiyiRecordings::_naming(qint64 podMs, bool podMsIsEnd) const
{
    QJsonObject naming = (podMs > 0) ? matchEvent(_events, podMs, podMsIsEnd) : QJsonObject();
    if (!naming.isEmpty()) {
        naming.insert(QStringLiteral("startMs"), naming.take(QStringLiteral("ms")));
        naming.insert(QStringLiteral("timeSource"), QStringLiteral("log"));
    } else {
        naming.insert(QStringLiteral("startMs"), podMs);
        naming.insert(QStringLiteral("timeSource"), podMs > 0 ? QStringLiteral("pod") : QString());
    }
    return naming;
}

QVariantList SiyiRecordings::files() const
{
    const int days = settings()->recordingAutoDeleteDays()->rawValue().toInt();
    const QDir store(storeDirectory());
    QVariantList result;
    QSet<QString> listed;

    const auto add = [&](const QString &key, const QVariantMap &pod) {
        const QJsonObject local = _index.value(key).toObject();
        const QFileInfo info(store.filePath(local.value(QStringLiteral("name")).toString()));
        const bool received = !local.isEmpty() && info.isFile();
        if (!received && pod.isEmpty()) {
            return;
        }
        QVariantMap item{
            { QStringLiteral("key"), key },
            { QStringLiteral("onPod"), !pod.isEmpty() },
            { QStringLiteral("received"), received },
            { QStringLiteral("podName"), pod.isEmpty() ? local.value(QStringLiteral("podName")).toString() : pod.value(QStringLiteral("name")).toString() },
            { QStringLiteral("podDir"), pod.isEmpty() ? local.value(QStringLiteral("podDir")).toString() : pod.value(QStringLiteral("dir")).toString() },
            { QStringLiteral("size"), pod.value(QStringLiteral("size"), -1) },
            { QStringLiteral("durationS"), -1 },
        };
        QJsonObject naming;
        if (received) {
            const qint64 receivedMs = info.lastModified().toMSecsSinceEpoch();
            item.insert(QStringLiteral("localName"), info.fileName());
            item.insert(QStringLiteral("localSize"), info.size());
            item.insert(QStringLiteral("size"), info.size());
            item.insert(QStringLiteral("receivedMs"), receivedMs);
            item.insert(QStringLiteral("deleteAtMs"), days > 0 ? receivedMs + days * kDayMs : 0);
            item.insert(QStringLiteral("durationS"), local.value(QStringLiteral("durationS")).toInt(-1));
            item.insert(QStringLiteral("podMs"), local.value(QStringLiteral("podMs")).toInteger());
            naming = local;
        } else {
            const qint64 podMs = pod.value(QStringLiteral("podMs")).toLongLong();
            item.insert(QStringLiteral("podMs"), podMs);
            naming = _naming(podMs, pod.value(QStringLiteral("podMsIsEnd")).toBool());
        }
        item.insert(QStringLiteral("startMs"), naming.value(QStringLiteral("startMs")).toInteger());
        item.insert(QStringLiteral("timeSource"), naming.value(QStringLiteral("timeSource")).toString());
        item.insert(QStringLiteral("hasPosition"), naming.contains(QStringLiteral("lat")));
        item.insert(QStringLiteral("lat"), naming.value(QStringLiteral("lat")).toDouble());
        item.insert(QStringLiteral("lon"), naming.value(QStringLiteral("lon")).toDouble());
        result.append(item);
    };

    for (const QVariantMap &pod : _pod) {
        const QString key = pod.value(QStringLiteral("key")).toString();
        listed.insert(key);
        add(key, pod);
    }
    for (auto it = _index.constBegin(); it != _index.constEnd(); ++it) {
        if (!listed.contains(it.key())) {
            add(it.key(), QVariantMap());
        }
    }

    // Newest first; a file of no known time after the rest, by name.
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap();
        const QVariantMap y = b.toMap();
        const qint64 xMs = x.value(QStringLiteral("startMs")).toLongLong();
        const qint64 yMs = y.value(QStringLiteral("startMs")).toLongLong();
        return (xMs != yMs) ? (xMs > yMs) : (x.value(QStringLiteral("podName")).toString() > y.value(QStringLiteral("podName")).toString());
    });
    return result;
}

QVariantMap SiyiRecordings::download() const
{
    return {
        { QStringLiteral("key"), _current },
        { QStringLiteral("percent"), _percent },
        { QStringLiteral("queued"), _queue },
        { QStringLiteral("error"), _downloadError },
    };
}

QUrl SiyiRecordings::_apiUrl(const QString &call, const QList<std::pair<QString, QString>> &query) const
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(settings()->ipAddress()->rawValue().toString());
    url.setPort(_mediaPort);
    url.setPath(QStringLiteral("/cgi-bin/media.cgi/api/v1/") + call);
    QUrlQuery items;
    for (const auto &[name, value] : query) {
        items.addQueryItem(name, value);
    }
    url.setQuery(items);
    return url;
}

QNetworkReply *SiyiRecordings::_get(const QUrl &url, int timeoutMs, bool head)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    return head ? _network->head(request) : _network->get(request);
}

void SiyiRecordings::refresh()
{
    if (listing()) {
        return;
    }
    _listError.clear();
    _listingPod.clear();
    QNetworkReply *const reply = _get(_apiUrl(QStringLiteral("getdirectories"), { { QStringLiteral("media_type"), kVideo } }), kApiTimeoutMs);
    _pendingReplies++;
    (void) connect(reply, &QNetworkReply::finished, this, [this, reply]() { _listedDirectories(reply); });
    emit listingChanged();
}

QJsonObject SiyiRecordings::_apiData(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        _listError = tr("카메라에 연결하지 못했습니다 (%1)").arg(reply->errorString());
        return {};
    }
    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
    if (!body.value(QStringLiteral("success")).toBool()) {
        _listError = tr("카메라가 목록을 주지 않았습니다 (%1)").arg(body.value(QStringLiteral("message")).toString());
        return {};
    }
    return body.value(QStringLiteral("data")).toObject();
}

void SiyiRecordings::_listedDirectories(QNetworkReply *reply)
{
    reply->deleteLater();
    const QJsonArray directories = _apiData(reply).value(QStringLiteral("directories")).toArray();
    for (const QJsonValue &directory : directories) {
        const QString dir = directory.toObject().value(QStringLiteral("path")).toString();
        QNetworkReply *const list = _get(_apiUrl(QStringLiteral("getmedialist"), {
            { QStringLiteral("media_type"), kVideo },
            { QStringLiteral("path"), dir },
            { QStringLiteral("start"), QStringLiteral("0") },
            { QStringLiteral("count"), kListCount },
        }), kApiTimeoutMs);
        _pendingReplies++;
        (void) connect(list, &QNetworkReply::finished, this, [this, list, dir]() { _listedMedia(list, dir); });
    }
    _replyDone();
}

void SiyiRecordings::_listedMedia(QNetworkReply *reply, const QString &dir)
{
    reply->deleteLater();
    const QString host = settings()->ipAddress()->rawValue().toString();
    const QJsonArray list = _apiData(reply).value(QStringLiteral("list")).toArray();
    for (const QJsonValue &value : list) {
        const QJsonObject file = value.toObject();
        const QString name = file.value(QStringLiteral("name")).toString();
        // The pod may answer with its factory address for its own files, as siyi-download.py notes.
        QUrl url = reply->url().resolved(QUrl(file.value(QStringLiteral("url")).toString()));
        url.setHost(host);
        if (name.isEmpty() || !url.isValid()) {
            continue;
        }
        const qint64 podMs = nameTime(name);
        const int index = static_cast<int>(_listingPod.size());
        _listingPod.append({
            { QStringLiteral("key"), dir + QLatin1Char('/') + name },
            { QStringLiteral("name"), name },
            { QStringLiteral("dir"), dir },
            { QStringLiteral("url"), url },
            { QStringLiteral("podMs"), podMs },
            { QStringLiteral("podMsIsEnd"), false },
            { QStringLiteral("size"), -1 },
        });
        if (podMs > 0) {
            continue;
        }
        // The list carries names and URLs alone, so a name with no time in it is dated by its file's
        // last write, which is where the recording ended.
        QNetworkReply *const head = _get(url, kApiTimeoutMs, true);
        _pendingReplies++;
        (void) connect(head, &QNetworkReply::finished, this, [this, head, index]() {
            head->deleteLater();
            const QDateTime modified = head->header(QNetworkRequest::LastModifiedHeader).toDateTime();
            const QVariant length = head->header(QNetworkRequest::ContentLengthHeader);
            if ((head->error() == QNetworkReply::NoError) && (index < _listingPod.size())) {
                if (modified.isValid()) {
                    _listingPod[index].insert(QStringLiteral("podMs"), modified.toMSecsSinceEpoch());
                    _listingPod[index].insert(QStringLiteral("podMsIsEnd"), true);
                }
                if (length.isValid()) {
                    _listingPod[index].insert(QStringLiteral("size"), length.toLongLong());
                }
            }
            _replyDone();
        });
    }
    _replyDone();
}

void SiyiRecordings::_replyDone()
{
    if (--_pendingReplies > 0) {
        return;
    }
    _pod = _listingPod;
    qCDebug(SiyiRecordingsLog) << "listed" << _pod.size() << "videos" << _listError;
    emit listingChanged();
    emit filesChanged();
}

QVariantMap SiyiRecordings::_podEntry(const QString &key) const
{
    for (const QVariantMap &entry : _pod) {
        if (entry.value(QStringLiteral("key")).toString() == key) {
            return entry;
        }
    }
    return {};
}

void SiyiRecordings::fetch(const QString &key)
{
    if ((key == _current) || _queue.contains(key) || _podEntry(key).isEmpty()) {
        return;
    }
    _queue.append(key);
    if (!_reply) {
        _startNext();
    } else {
        emit downloadChanged();
    }
}

void SiyiRecordings::fetchAll()
{
    for (const QVariant &file : files()) {
        const QVariantMap item = file.toMap();
        if (item.value(QStringLiteral("onPod")).toBool() && !item.value(QStringLiteral("received")).toBool()) {
            fetch(item.value(QStringLiteral("key")).toString());
        }
    }
}

void SiyiRecordings::cancel()
{
    _queue.clear();
    if (_reply) {
        _reply->abort();   // _downloaded() clears up
    } else {
        emit downloadChanged();
    }
}

void SiyiRecordings::_setDownloadError(const QString &error)
{
    qCWarning(SiyiRecordingsLog) << _current << error;
    _downloadError = error;
}

void SiyiRecordings::_startNext()
{
    _current.clear();
    _currentEntry.clear();
    while (!_queue.isEmpty()) {
        const QString key = _queue.takeFirst();
        const QVariantMap entry = _podEntry(key);
        if (entry.isEmpty()) {
            continue;
        }
        _current = key;
        _currentEntry = entry;
        _part = new QFile(QDir(storeDirectory()).filePath(kPartFile), this);
        if (!QDir().mkpath(storeDirectory()) || !_part->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            _setDownloadError(tr("태블릿에 저장하지 못했습니다"));
            delete _part;
            _part = nullptr;
            _current.clear();
            continue;
        }
        _percent = 0;
        _downloadError.clear();
        const qint64 knownSize = entry.value(QStringLiteral("size")).toLongLong();
        _reply = _get(entry.value(QStringLiteral("url")).toUrl(), kDownloadStallMs);
        // Counted here rather than off downloadProgress, which Qt holds back for the first 100 ms
        // and so never reports a transfer that arrives in one burst and then stalls.
        (void) connect(_reply, &QNetworkReply::readyRead, this, [this, knownSize]() {
            if (!_reply || !_part) {
                return;
            }
            (void) _part->write(_reply->readAll());
            const QVariant length = _reply->header(QNetworkRequest::ContentLengthHeader);
            const qint64 total = length.isValid() ? length.toLongLong() : knownSize;
            const int percent = (total > 0) ? static_cast<int>(std::clamp<qint64>(_part->pos() * 100 / total, 0, 100)) : 0;
            if (percent != _percent) {
                _percent = percent;
                emit downloadChanged();
            }
        });
        (void) connect(_reply, &QNetworkReply::finished, this, &SiyiRecordings::_downloaded);
        emit downloadChanged();
        return;
    }
    emit downloadChanged();
}

void SiyiRecordings::_downloaded()
{
    QNetworkReply *const reply = _reply;
    _reply = nullptr;
    if (!reply || !_part) {
        return;
    }
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) {
        (void) _part->write(reply->readAll());
    }
    const bool written = _part->flush() && (_part->error() == QFileDevice::NoError);
    _part->close();
    const QString partPath = _part->fileName();
    delete _part;
    _part = nullptr;

    if ((reply->error() != QNetworkReply::NoError) || !written) {
        (void) QFile::remove(partPath);
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            _setDownloadError(written ? tr("받지 못했습니다 (%1)").arg(reply->errorString()) : tr("태블릿에 저장하지 못했습니다"));
        }
        _startNext();
        return;
    }

    // The pod's time for the file: from its name, or its last write as the listing or this download
    // reported it. With none, the moment it arrived, and the list says so.
    const qint64 receivedMs = QDateTime::currentMSecsSinceEpoch();
    qint64 podMs = _currentEntry.value(QStringLiteral("podMs")).toLongLong();
    bool podMsIsEnd = _currentEntry.value(QStringLiteral("podMsIsEnd")).toBool();
    if (podMs <= 0) {
        const QDateTime modified = reply->header(QNetworkRequest::LastModifiedHeader).toDateTime();
        if (modified.isValid()) {
            podMs = modified.toMSecsSinceEpoch();
            podMsIsEnd = true;
        }
    }
    QJsonObject record = _naming(podMs, podMsIsEnd);
    if (podMs <= 0) {
        record.insert(QStringLiteral("startMs"), receivedMs);
        record.insert(QStringLiteral("timeSource"), QStringLiteral("download"));
    }
    const bool withPosition = (settings()->recordingFileName()->rawValue().toUInt() == 1) && record.contains(QStringLiteral("lat"));
    const QString name = rfpName(record.value(QStringLiteral("startMs")).toInteger(), withPosition,
                                 record.value(QStringLiteral("lat")).toDouble(), record.value(QStringLiteral("lon")).toDouble());

    // Never over another copy: two files named alike keep both, the second with a number.
    const QDir store(storeDirectory());
    QString localName = name;
    for (int n = 2; store.exists(localName); n++) {
        localName = QFileInfo(name).completeBaseName() + QStringLiteral("_%1.mp4").arg(n);
    }
    if (!QFile::rename(partPath, store.filePath(localName))) {
        (void) QFile::remove(partPath);
        _setDownloadError(tr("태블릿에 저장하지 못했습니다"));
        _startNext();
        return;
    }

    record.insert(QStringLiteral("name"), localName);
    record.insert(QStringLiteral("podName"), _currentEntry.value(QStringLiteral("name")).toString());
    record.insert(QStringLiteral("podDir"), _currentEntry.value(QStringLiteral("dir")).toString());
    record.insert(QStringLiteral("podMs"), podMs);
    record.insert(QStringLiteral("durationS"), mp4DurationSeconds(store.filePath(localName)));
    _index.insert(_current, record);
    if (!_saveIndex()) {
        qCWarning(SiyiRecordingsLog) << "could not write the index";
    }
    qCDebug(SiyiRecordingsLog) << "received" << _current << "as" << localName;
    emit filesChanged();
    _startNext();
}

bool SiyiRecordings::remove(const QString &key)
{
    const QString name = _index.value(key).toObject().value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        return false;
    }
    const QString path = QDir(storeDirectory()).filePath(name);
    if (QFile::exists(path) && !QFile::remove(path)) {
        return false;
    }
    _index.remove(key);
    (void) _saveIndex();
    emit filesChanged();
    return true;
}

QString SiyiRecordings::exportCopy(const QString &key)
{
    const QString name = _index.value(key).toObject().value(QStringLiteral("name")).toString();
    const QString source = QDir(storeDirectory()).filePath(name);
    const QString folder = SettingsManager::instance()->appSettings()->videoSavePath();
    if (name.isEmpty() || !QFile::exists(source) || folder.isEmpty() || !QDir().mkpath(folder)) {
        return QString();
    }
    const QString target = QDir(folder).filePath(name);
    // Exported before: the same file is there already.
    return (QFile::exists(target) || QFile::copy(source, target)) ? target : QString();
}

QUrl SiyiRecordings::playbackUrl(const QString &key) const
{
    const QString name = _index.value(key).toObject().value(QStringLiteral("name")).toString();
    const QString path = QDir(storeDirectory()).filePath(name);
    return (!name.isEmpty() && QFile::exists(path)) ? QUrl::fromLocalFile(path) : QUrl();
}

int SiyiRecordings::purge()
{
    const int days = settings()->recordingAutoDeleteDays()->rawValue().toInt();
    if (days <= 0) {
        return 0;
    }
    // Aged from its arrival on the tablet: the file's last write is the end of its download.
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
    const QDir store(storeDirectory());
    int removed = 0;
    bool changed = false;
    for (auto it = _index.begin(); it != _index.end();) {
        const QFileInfo info(store.filePath(it->toObject().value(QStringLiteral("name")).toString()));
        if (info.isFile() && (info.lastModified() >= cutoff)) {
            ++it;
            continue;
        }
        if (info.isFile()) {
            if (!QFile::remove(info.filePath())) {
                ++it;
                continue;
            }
            removed++;
        }
        // A copy gone from under the index is dropped from it too.
        it = _index.erase(it);
        changed = true;
    }
    if (changed) {
        qCDebug(SiyiRecordingsLog) << "auto delete removed" << removed << "copies older than" << days << "days";
        (void) _saveIndex();
        emit filesChanged();
    }
    return removed;
}

bool SiyiRecordings::_saveIndex() const
{
    return writeJson(kIndexFile, QJsonDocument(_index));
}

bool SiyiRecordings::_saveEvents() const
{
    return writeJson(kEventsFile, QJsonDocument(_events));
}
