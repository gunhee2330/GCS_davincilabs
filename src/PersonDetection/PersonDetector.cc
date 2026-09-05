#include "PersonDetector.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QMutexLocker>
#include <QtQml/QJSEngine>

#include "QGCLoggingCategory.h"
#include "VideoManager.h"
#include "VideoReceiver.h"

QGC_LOGGING_CATEGORY(PersonDetectorLog, "PersonDetection.PersonDetector")

Q_APPLICATION_STATIC(PersonDetector, _personDetectorInstance, nullptr);

PersonDetector::PersonDetector(QObject* parent)
    : QObject(parent)
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
    if (!_loadWorker()) {
        qCDebug(PersonDetectorLog) << "model unavailable, person detection inactive";
        _thread.quit();
        (void) _thread.wait();
        return;
    }

    VideoManager* const videoManager = VideoManager::instance();
    videoManager->setFrameTapEnabled(true);
    // Direct: submit() is a mailbox handoff, so it must not queue frames on the streaming thread.
    (void) connect(videoManager, &VideoManager::videoFrameTapped, this, &PersonDetector::submit,
                   Qt::DirectConnection);

    _active = true;
    emit activeChanged();
}

void PersonDetector::submit(const TappedVideoFrame& frame)
{
    QMutexLocker locker(&_mutex);
    if (_shuttingDown) {
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
    (void) QMetaObject::invokeMethod(&_worker, [this, &loaded]() { loaded = _worker.load(); },
                                     Qt::BlockingQueuedConnection);
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
    const QList<QRectF> boxes = _worker.detect(frame, &inferenceMs);

    (void) QMetaObject::invokeMethod(this, [this, boxes, inferenceMs]() {
        _boxes = boxes;
        _inferenceMs = inferenceMs;
        emit detectionsChanged();
        qCDebug(PersonDetectorLog) << "persons" << _boxes.size()
                                   << "ms" << _inferenceMs;
    }, Qt::QueuedConnection);
}
