#pragma once

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QRectF>
#include <QtCore/QThread>
#include <QtGui/QImage>
#include <QtQmlIntegration/QtQmlIntegration>

#include "PersonDetectorWorker.h"

Q_DECLARE_LOGGING_CATEGORY(PersonDetectorLog)

class QQmlEngine;
class QJSEngine;
struct TappedVideoFrame;

/// \brief Live person and vehicle counts and boxes for the EO video stream.
///
/// Frames arrive from VideoManager's tap on the streaming thread and are handed to a single
/// worker thread. Inference is slower than the stream, so submissions land in a one-slot
/// mailbox: while a frame is being processed, newer frames overwrite each other and only the
/// freshest one is picked up next. Boxes are normalised 0..1 in the source frame, so QML maps
/// them with VideoOutput.contentRect.
class PersonDetector : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool             active       READ active         NOTIFY activeChanged)
    Q_PROPERTY(int              count        READ count          NOTIFY detectionsChanged)
    Q_PROPERTY(QList<QRectF>    boxes        READ boxes          NOTIFY detectionsChanged)
    Q_PROPERTY(int              vehicleCount READ vehicleCount   NOTIFY detectionsChanged)
    Q_PROPERTY(QList<QRectF>    vehicleBoxes READ vehicleBoxes   NOTIFY detectionsChanged)
    Q_PROPERTY(int              inferenceMs  READ inferenceMs    NOTIFY detectionsChanged)

    friend class PersonDetectorTest;

public:
    /// No default argument: a default-constructible QML_SINGLETON is default-constructed by the
    /// engine instead of going through create(), which would give QML a second detector.
    explicit PersonDetector(QObject* parent);
    ~PersonDetector();

    static PersonDetector* instance();
    static PersonDetector* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// Loads the model and starts consuming the video tap. Stays inactive when the model is
    /// unavailable. Call once, after VideoManager::init().
    void init();

    /// Callable from any thread. Keeps only the newest frame while inference is busy.
    void submit(const TappedVideoFrame& frame);

    [[nodiscard]] bool active() const { return _active; }
    [[nodiscard]] int count() const { return static_cast<int>(_boxes.size()); }
    [[nodiscard]] QList<QRectF> boxes() const { return _boxes; }
    [[nodiscard]] int vehicleCount() const { return static_cast<int>(_vehicleBoxes.size()); }
    [[nodiscard]] QList<QRectF> vehicleBoxes() const { return _vehicleBoxes; }
    [[nodiscard]] int inferenceMs() const { return _inferenceMs; }

signals:
    void activeChanged();
    void detectionsChanged();

private:
    bool _loadWorker();
    void _runNextFrame();

    QThread _thread;
    PersonDetectorWorker _worker;

    QMutex _mutex;
    QImage _pending;             ///< Mailbox slot, guarded by _mutex
    bool _runPending = false;    ///< A worker run is queued or in flight
    bool _shuttingDown = false;  ///< Stops submit() from reaching a worker being torn down

    QList<QRectF> _boxes;
    QList<QRectF> _vehicleBoxes;
    int _inferenceMs = 0;
    bool _active = false;
};
