#pragma once

#include <memory>
#include <vector>

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QRectF>
#include <QtGui/QImage>

Q_DECLARE_LOGGING_CATEGORY(PersonDetectorWorkerLog)

/// \brief Runs the bundled YOLOv8n ONNX model, one frame at a time.
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

    /// Creates the inference session from the compiled-in model. False when the model is
    /// missing or unusable; safe to call again.
    bool load();

    /// Persons and vehicles from a single inference pass, empty when not loaded or on failure.
    /// @a inferenceMs (optional) receives the wall time of the whole detect step.
    Detections detect(const QImage& frame, int* inferenceMs);

private:
    struct Session;  ///< Keeps the ONNX Runtime types out of this header

    std::unique_ptr<Session> _session;
    std::vector<float> _input;  ///< NCHW tensor buffer, reused across frames
};
