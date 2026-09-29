#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(SiyiRecordingsLog)

class QFile;
class QJSEngine;
class QNetworkAccessManager;
class QNetworkReply;
class QQmlEngine;

/// \brief The pod's recorded videos and their copies on this tablet, for the 영상 기록 page.
///
/// The procurement spec wants video records easy to check (p6 차), a module that stores, deletes
/// on schedule and limits who may do what (p5), and files named after the recording's start,
/// optionally with where the aircraft was (p9). The pod keeps its videos on its own SD card and
/// names them itself, so this lists them over the pod's HTTP media API (port 82, the calls
/// ArduPilot's siyi-download.py makes), downloads a copy into the app's private data under the
/// RFP name, and keeps a log of when the pod started recording and where the aircraft was then,
/// which is what that name is built from.
class SiyiRecordings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Every video on the pod or on this tablet, newest first. Each is a map: key, podName, podDir,
    /// onPod, received, localName, localSize, receivedMs, deleteAtMs (0 when auto delete is off),
    /// size (bytes, -1 unknown), durationS (-1 unknown), podMs (the pod's own time for the file,
    /// 0 unknown), startMs (the time the name carries, 0 unknown), timeSource (where startMs came
    /// from: "log" a logged recording start, "pod" the pod's file time, "download" the moment it was
    /// received, "" not known yet), hasPosition, lat, lon.
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    /// The pod is being listed
    Q_PROPERTY(bool listing READ listing NOTIFY listingChanged)
    /// Why the last listing failed, empty when it did not
    Q_PROPERTY(QString listError READ listError NOTIFY listingChanged)
    /// The download under way: key, percent, queued (keys still to come) and error (why the last
    /// one failed, empty when it did not)
    Q_PROPERTY(QVariantMap download READ download NOTIFY downloadChanged)

public:
    /// No default argument, for the reason SiyiCameraController gives.
    explicit SiyiRecordings(QObject *parent);
    ~SiyiRecordings() override;

    static SiyiRecordings *instance();
    static SiyiRecordings *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    /// Starts the recording log and the auto delete, now and daily. Once, from
    /// SiyiCameraController::init().
    void init();

    [[nodiscard]] QVariantList files() const;
    [[nodiscard]] bool listing() const { return _pendingReplies > 0; }
    [[nodiscard]] QString listError() const { return _listError; }
    [[nodiscard]] QVariantMap download() const;

    /// Lists the pod's video folders and the videos in each.
    Q_INVOKABLE void refresh();
    /// Downloads one listed video, after the one under way if any.
    Q_INVOKABLE void fetch(const QString &key);
    /// Downloads every listed video not on the tablet yet.
    Q_INVOKABLE void fetchAll();
    /// Stops the download under way and drops the ones waiting.
    Q_INVOKABLE void cancel();
    /// Deletes the tablet's copy.
    Q_INVOKABLE bool remove(const QString &key);
    /// Copies the tablet's copy to the app's video folder. Returns the copy's path, or empty.
    Q_INVOKABLE QString exportCopy(const QString &key);
    /// What the player opens for the tablet's copy.
    Q_INVOKABLE QUrl playbackUrl(const QString &key) const;

    /// Deletes the copies older than the auto delete setting. Returns how many went.
    int purge();

    /// Where the copies, their index and the recording log are kept: the app's private data.
    static QString storeDirectory();

    /// The RFP p9 name: yyyyMMddhhmmsszzz.mp4, or yyyyMMddhhmmsszzz_lat_lon.mp4 with four decimals.
    static QString rfpName(qint64 startMs, bool withPosition, double lat, double lon);

    /// The date and time a pod file's name carries (local time), in ms since the epoch; 0 when none.
    static qint64 nameTime(const QString &podName);

    /// The recording start in \a events (each {ms, lat, lon}) that a pod file of time \a podMs came from,
    /// or an empty object. \a podMsIsEnd when \a podMs is the file's last write rather than its start.
    static QJsonObject matchEvent(const QJsonArray &events, qint64 podMs, bool podMsIsEnd);

    /// The length of an MP4 from its movie header, or -1.
    static int mp4DurationSeconds(const QString &path);

    /// The recording log, oldest first.
    [[nodiscard]] QJsonArray events() const { return _events; }
    /// Adds a recording start to the log.
    void logRecordingStart(qint64 ms, const QGeoCoordinate &position);

signals:
    void filesChanged();
    void listingChanged();
    void downloadChanged();

private:
    /// The pod's link and recording state as the controller reports them. A start is logged only on
    /// a change seen while connected: the first report after (re)connecting is the baseline, so a
    /// recording already running then is not taken for one starting.
    void _podState(bool connected, bool recording);
    /// Parses a media API answer; empty with _listError set when it failed.
    QJsonObject _apiData(QNetworkReply *reply);
    QUrl _apiUrl(const QString &call, const QList<std::pair<QString, QString>> &query) const;
    /// The port the pod's media API answers on at \a host, the address in the pod's settings.
    quint16 _mediaPortFor(const QString &host) const;
    QNetworkReply *_get(const QUrl &url, int timeoutMs, bool head = false);
    void _listedDirectories(QNetworkReply *reply);
    void _listedMedia(QNetworkReply *reply, const QString &dir);
    void _replyDone();
    void _startNext();
    void _downloaded();
    void _setDownloadError(const QString &error);
    bool _saveIndex() const;
    bool _saveEvents() const;
    /// The pod entry for \a key, or an empty map.
    QVariantMap _podEntry(const QString &key) const;
    /// startMs, timeSource and, when known, lat and lon for a pod file of \a podMs.
    QJsonObject _naming(qint64 podMs, bool podMsIsEnd) const;

    QNetworkAccessManager *_network = nullptr;
    /// The pod's media API port. Fixed on the pod; tests point it at a local server.
    quint16 _mediaPort = 82;

    /// The pod's videos as last listed: key, name, dir, url, podMs, podMsIsEnd, size.
    QList<QVariantMap> _pod;
    QList<QVariantMap> _listingPod;
    int _pendingReplies = 0;
    QString _listError;

    /// The tablet's copies by key: name, podName, podDir, podMs, startMs, timeSource, lat, lon, durationS.
    QJsonObject _index;
    QJsonArray _events;

    QPointer<QNetworkReply> _reply;
    QFile *_part = nullptr;
    QString _current;
    QVariantMap _currentEntry;
    QStringList _queue;
    int _percent = 0;
    QString _downloadError;

    bool _initialized = false;
    bool _recordingKnown = false;
    bool _podRecording = false;
    QTimer _purgeTimer;

    friend class SiyiRecordingsTest;
    friend class PoliceRecordingsUITest;
};
