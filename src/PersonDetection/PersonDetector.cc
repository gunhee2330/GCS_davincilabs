#include "PersonDetector.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QMutexLocker>
#include <QtCore/QSettings>
#include <QtQml/QJSEngine>

#include "QGCLoggingCategory.h"
#include "VideoManager.h"
#include "VideoReceiver.h"

QGC_LOGGING_CATEGORY(PersonDetectorLog, "PersonDetection.PersonDetector")

Q_APPLICATION_STATIC(PersonDetector, _personDetectorInstance, nullptr);

namespace {
constexpr auto kEnabledSettingsKey = QLatin1StringView("PersonDetection/enabled");
}  // namespace

PersonDetector::PersonDetector(QObject* parent)
    : QObject(parent), _enabled(QSettings().value(kEnabledSettingsKey, true).toBool())
{
    _worker.moveToThread(&_thread);
    _thread.setObjectName(QStringLiteral("PersonDetector"));
    _thread.start();
}

PersonDetector::~PersonDetector()
{
    {
        QMutexLocker locker(&_mutex);
        _shuttingDown = true;
    }
    _thread.quit();
    (void) _thread.wait();
}

PersonDetector* PersonDetector::instance()
{
    return _personDetectorInstance();
}

PersonDetector* PersonDetector::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    PersonDetector* const detector = instance();
    QJSEngine::setObjectOwnership(detector, QJSEngine::CppOwnership);
    return detector;
}

void PersonDetector::init()
{
    _loaded = _loadWorker();
    if (!_loaded) {
        qCDebug(PersonDetectorLog) << "model unavailable, person detection inactive";
        _thread.quit();
        (void) _thread.wait();
        return;
    }

    VideoManager* const videoManager = VideoManager::instance();
    if (!videoManager) {
        qCWarning(PersonDetectorLog) << "no VideoManager, person detection inactive";
        return;
    }

    // Link the tap unconditionally: VideoReceiver only reads this flag when decoding starts, so a
    // later switch-on could not re-link it. submit() drops frames instead while the switch is off.
    videoManager->setFrameTapEnabled(true);
    // Direct: submit() is a mailbox handoff, so it must not queue frames on the streaming thread.
    (void) connect(videoManager, &VideoManager::videoFrameTapped, this, &PersonDetector::submit,
                   Qt::DirectConnection);

    _active = _enabled;
    emit activeChanged();
}

void PersonDetector::setEnabled(bool enabled)
{
    if (_enabled == enabled) {
        return;
    }
    _enabled = enabled;
    QSettings().setValue(kEnabledSettingsKey, enabled);
    emit enabledChanged();

    if (!enabled) {
        {
            QMutexLocker locker(&_mutex);
            _pending = QImage();
        }
        _boxes.clear();
        emit detectionsChanged();
    }

    const bool active = _loaded && enabled;
    if (_active != active) {
        _active = active;
        emit activeChanged();
    }
}

void PersonDetector::submit(const TappedVideoFrame& frame)
{
    // Checked under the lock: a frame that read the switch just before setEnabled(false) would
    // otherwise refill the mailbox after it was cleared, costing one inference while off.
    QMutexLocker locker(&_mutex);
    if (!_enabled || _shuttingDown) {
        return;
    }

    _pending = frame.image;
    if (_runPending) {
        return;
    }
    _runPending = true;
    (void) QMetaObject::invokeMethod(&_worker, [this]() { _runNextFrame(); }, Qt::QueuedConnection);
}

bool PersonDetector::_loadWorker()
{
    if (!_thread.isRunning()) {
        return false;
    }

    bool loaded = false;
    (void) QMetaObject::invokeMethod(&_worker, [this, &loaded]() {
        loaded = _worker.load();
    }, Qt::BlockingQueuedConnection);
    return loaded;
}

void PersonDetector::_runNextFrame()
{
    QImage frame;
    {
        QMutexLocker locker(&_mutex);
        frame = _pending;
        _pending = QImage();
        _runPending = false;
    }
    if (frame.isNull()) {
        return;
    }

    int inferenceMs = 0;
    // detections.vehicles is dropped on the floor: the shipped descriptor names no vehicle class,
    // and vehicle counts come from the module now. The worker's vehicle path is left alone so
    // swapping in a model that has those classes stays a file change rather than a code change.
    const PersonDetectorWorker::Detections detections = _worker.detect(frame, &inferenceMs);

    (void) QMetaObject::invokeMethod(this, [this, boxes = detections.persons, inferenceMs]() {
        if (!_enabled) {
            return;  // Switched off while this frame was in flight
        }
        _boxes = boxes;
        emit detectionsChanged();
        qCDebug(PersonDetectorLog) << "persons" << _boxes.size() << "ms" << inferenceMs;
    }, Qt::QueuedConnection);
}
