#pragma once

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QRectF>
#include <QtCore/QThread>
#include <QtGui/QImage>
#include <QtQmlIntegration/QtQmlIntegration>

#include <atomic>

#include "PersonDetectorWorker.h"

Q_DECLARE_LOGGING_CATEGORY(PersonDetectorLog)

class QQmlEngine;
class QJSEngine;
struct TappedVideoFrame;

/// \brief Person boxes for the face mosaic drawn over the EO video stream.
///
/// Counting is the AI module's job now. Its undocumented 0xD5 command pushes per-class tallies
/// and nothing else, and the marks the operator sees are burned into the module's own RTSP feed,
/// so no object coordinate ever reaches this process from there. What is left for a detector on
/// this side is the one thing the module cannot supply: somewhere to put a mosaic in our own
/// windows. Hence boxes and no numbers - a count taken here would be a second, disagreeing
/// answer to a question the module has already answered.
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
    Q_PROPERTY(bool             enabled      READ enabled        WRITE setEnabled NOTIFY enabledChanged)

    /// Whole person boxes, not head boxes. Two consumers share them and they disagree about what
    /// a good box is: the mosaic wants the top slice of one, while PoliceDroneCameraPanel's long
    /// press hands a whole box to the module's tracker, which follows whatever it is given and
    /// would follow a head. Cropping here would serve the first and break the second, so the crop
    /// stays in the overlay that draws it.
    Q_PROPERTY(QList<QRectF>    boxes        READ boxes          NOTIFY detectionsChanged)

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
    [[nodiscard]] bool enabled() const { return _enabled; }
    [[nodiscard]] QList<QRectF> boxes() const { return _boxes; }

    /// Operator switch, persisted across runs. Its own switch on the tool strip rather than the
    /// AI one: to the operator "AI" is the module, and the mosaic is both a personal-data measure
    /// that outlives any recognition setting and the only thing here costing ~100 ms of tablet
    /// CPU a frame. Switching off drops tapped frames before inference and clears the boxes;
    /// switching on resumes only if the model loaded.
    void setEnabled(bool enabled);

signals:
    void activeChanged();
    void enabledChanged();
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
    bool _loaded = false;        ///< Model loaded by init(); the switch cannot activate the detector without it
    std::atomic<bool> _enabled;  ///< Operator switch, restored from QSettings; read by submit() on the stream thread
    bool _active = false;
};
