#pragma once

#include <memory>
#include <vector>

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtGui/QImage>

Q_DECLARE_LOGGING_CATEGORY(PersonDetectorWorkerLog)

/// \brief Runs the bundled person detection ONNX model, one frame at a time.
///
/// Lives on PersonDetector's worker thread; every call blocks for the length of an inference,
/// so nothing here may be touched from the UI thread. Builds without ONNX Runtime keep the
/// class but load() always fails, which leaves the detector inactive.
class PersonDetectorWorker : public QObject
{
    Q_OBJECT

public:
    /// Boxes normalised 0..1 in the source frame's coordinates, best score first.
    struct Detections
    {
        QList<QRectF> persons;
        QList<QRectF> vehicles;
    };

    explicit PersonDetectorWorker(QObject* parent = nullptr);
    ~PersonDetectorWorker();

    /// Creates the inference session from the compiled-in model, taking its input and output shape
    /// from the session and its output layout and class mapping from the descriptor beside it. False
    /// when either is missing, unusable, or disagrees with the other; safe to call again.
    bool load();

    /// False when the loaded model has no vehicle class at all, which makes a vehicle count of zero
    /// meaningless rather than true. False as well until load() has succeeded.
    [[nodiscard]] bool detectsVehicles() const;

    /// Regions the current sweep covers the frame in, one per detect() call before it wraps.
    /// Zero until the first frame sized it.
    [[nodiscard]] int regionCount() const { return static_cast<int>(_regions.size()); }

    /// Persons and vehicles in the frame, empty when not loaded or on failure. The frame is
    /// covered by one pass over the whole picture plus a grid of tiles, since a person seen
    /// from the air is only a few pixels tall once a wide frame is squeezed into the model's
    /// own square input. @a inferenceMs (optional) receives the wall time of all passes.
    Detections detect(const QImage& frame, int* inferenceMs);

private:
    struct Session;  ///< Keeps the ONNX Runtime types out of this header

    /// One inference over @a region, returning boxes normalised inside that region.
    Detections _detectRegion(const QImage& region);

    std::unique_ptr<Session> _session;
    std::vector<float> _input;      ///< NCHW tensor buffer, reused across frames
    QSize _lastFrameSize;           ///< Also logged when it changes: the stream's resolution
                                    ///< sets the altitude a person is still detectable at
    QImage _scenePrint;             ///< Thumbnail of the last frame: a big change ends the sweep
    QList<Detections> _regions;     ///< Last result of each region, index 0 the whole frame
    int _regionCursor = 0;          ///< Region this frame covers; the sweep rolls one per frame
};
