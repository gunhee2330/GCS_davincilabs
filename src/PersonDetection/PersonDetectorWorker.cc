#include "PersonDetectorWorker.h"

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>

#include <vector>

#include "PersonDetectorCore.h"
#include "QGCLoggingCategory.h"

#ifdef QGC_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#endif

QGC_LOGGING_CATEGORY(PersonDetectorWorkerLog, "PersonDetection.PersonDetectorWorker")

#ifdef QGC_HAS_ONNXRUNTIME

namespace {

/// Device tuning knob: 4 matches the tablet's big-core cluster. Raise or lower per device.
constexpr int kThreads = 4;

/// The frame is covered in tiles smaller than the model's own input, so a person is enlarged
/// on the way in rather than shrunk with the rest of the frame: at 1280x720 a whole frame pass
/// squeezes a 30 px person down to 8 px and finds nothing, while a 192 px tile hands the model
/// a 50 px one. Tiles overlap because a person cut by a tile edge scores as neither half.
/// Measured on one overhead crowd: whole frame 0, 320 px tiles 47, 192 px tiles 65, and 192 px
/// tiles with this overlap 122. Smaller and denser keeps helping, at a tile per inference.
/// The tile is a fraction of the model's own input rather than a fixed 192 px, so a model exported
/// at another input size still enlarges a person by the same amount (0.6 of 320 is that 192 px).
constexpr qreal kTileToInput = 0.6;
constexpr qreal kTileOverlap = 0.25;

/// Crowds really do overlap, so suppression inside a tile is looser than the usual 0.45, and
/// the merge across tiles looser still — the same person seen by two tiles is one person.
constexpr float kTileNmsIou = 0.6f;
constexpr float kMergeIou = 0.55f;

/// A sweep is one inference per region, so this is also its length: 64 regions is about eight
/// seconds on the tablet, which is as stale as a bracket may get.
constexpr int kMaxRegions = 64;

/// Tile origins along one axis: stepped by the overlap, with the last one pulled back to the
/// edge so the far side is covered too.
std::vector<int> tileOrigins(int length, int tilePx)
{
    std::vector<int> origins;
    const int step = qMax(1, qRound(tilePx * (1.0 - kTileOverlap)));
    for (int at = 0; (at + tilePx) <= length; at += step) {
        origins.push_back(at);
    }
    if (origins.empty()) {
        origins.push_back(0);
    } else if (origins.back() + tilePx < length) {
        origins.push_back(length - tilePx);
    }
    return origins;
}

/// The model and the descriptor beside it, which names its output layout and the class ids it counts
/// as people and as vehicles. One base name for both, so a swap cannot leave one model's weights
/// beside another model's descriptor: swapping models is this line and the pair of files in models/.
constexpr const char* kModelResourceBase = ":/PersonDetection/rtmdet-n-person";

/// One environment per process, as ONNX Runtime expects.
Ort::Env& ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "QGCPersonDetector");
    return env;
}

/// ONNX states an axis whose length is decided at run time as a negative dim. Two of them are fine
/// here and the rest are not: one image goes in whatever the batch axis says, and however many
/// detections come back are read back. Every other axis is sized into a buffer or a loop bound.
constexpr bool dimIsOneOrDynamic(qint64 dim) { return (dim == 1) || (dim < 0); }
constexpr bool dimIsCountOrDynamic(qint64 dim) { return dim != 0; }

/// load() vetted the type the model declares; this vets the tensor that actually came back, since
/// the decode reads it as that type.
bool tensorIs(const Ort::Value& value, ONNXTensorElementDataType type)
{
    return value.IsTensor() && (value.GetTensorTypeAndShapeInfo().GetElementType() == type);
}

}  // namespace

/// The model's own shape and mapping, read once at load: everything downstream is sized from
/// these rather than from constants, so another export needs no code change.
struct PersonDetectorWorker::Session
{
    Ort::SessionOptions options;
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::unique_ptr<Ort::Session> session;
    std::string inputName;                 ///< "images"/"output0" on a stock export, but not assumed to be
    std::vector<std::string> outputNames;  ///< One for Yolo, 'dets' then 'labels' for DetsLabels
    int inputSize = 0;                     ///< Square NCHW side
    int numClasses = 0;                    ///< Yolo layout only: the rest of its output rows
    int numAnchors = 0;                    ///< Yolo layout only
    int tilePx = 0;
    PersonDetectorCore::ModelDescriptor descriptor;
};

PersonDetectorWorker::PersonDetectorWorker(QObject* parent)
    : QObject(parent)
{
}

PersonDetectorWorker::~PersonDetectorWorker() = default;

bool PersonDetectorWorker::load()
{
    if (_session) {
        return true;
    }

    const QString resourceBase = QString::fromLatin1(kModelResourceBase);

    QFile modelFile(resourceBase + QStringLiteral(".onnx"));
    if (!modelFile.open(QIODevice::ReadOnly)) {
        qCWarning(PersonDetectorWorkerLog) << "model resource unavailable:" << modelFile.fileName();
        return false;
    }
    const QByteArray model = modelFile.readAll();

    QFile descriptorFile(resourceBase + QStringLiteral(".json"));
    if (!descriptorFile.open(QIODevice::ReadOnly)) {
        qCWarning(PersonDetectorWorkerLog) << "model descriptor unavailable:" << descriptorFile.fileName();
        return false;
    }

    // Parsed before the session: the descriptor states the layout, and the layout decides which
    // shapes the model is allowed to have below.
    QString descriptorError;
    const std::optional<PersonDetectorCore::ModelDescriptor> descriptor =
        PersonDetectorCore::parseModelDescriptor(descriptorFile.readAll(), &descriptorError);
    if (!descriptor) {
        qCWarning(PersonDetectorWorkerLog) << "model descriptor rejected:" << descriptorError;
        return false;
    }
    const bool yolo = (descriptor->layout == PersonDetectorCore::OutputLayout::Yolo);

    try {
        auto session = std::make_unique<Session>();
        session->options.SetIntraOpNumThreads(kThreads);
        session->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session->session = std::make_unique<Ort::Session>(ortEnv(), model.constData(),
                                                         static_cast<size_t>(model.size()), session->options);

        const size_t outputCount = yolo ? 1 : 2;
        if ((session->session->GetInputCount() != 1) || (session->session->GetOutputCount() != outputCount)) {
            qCWarning(PersonDetectorWorkerLog) << "model does not have one input and" << outputCount
                                               << "outputs, as its layout says it should";
            return false;
        }
        Ort::AllocatorWithDefaultOptions allocator;
        session->inputName = session->session->GetInputNameAllocated(0, allocator).get();
        for (size_t i = 0; i < outputCount; ++i) {
            session->outputNames.emplace_back(session->session->GetOutputNameAllocated(i, allocator).get());
        }

        // Shape as a QList so a rejected one prints, and so a dynamic axis shows up as the -1 it is.
        // The element type comes along because every tensor below is indexed as one type or the other.
        struct TensorSpec
        {
            QList<qint64> shape;
            ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
        };
        const auto specOf = [](const Ort::TypeInfo& info) {
            const auto tensor = info.GetTensorTypeAndShapeInfo();
            const std::vector<int64_t> dims = tensor.GetShape();
            return TensorSpec{QList<qint64>(dims.cbegin(), dims.cend()), tensor.GetElementType()};
        };
        constexpr auto kFloat = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
        constexpr auto kInt64 = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;

        const TensorSpec input = specOf(session->session->GetInputTypeInfo(0));
        if ((input.type != kFloat) || (input.shape.size() != 4) || !dimIsOneOrDynamic(input.shape[0])
            || (input.shape[1] != 3) || (input.shape[2] < 1) || (input.shape[2] != input.shape[3])) {
            qCWarning(PersonDetectorWorkerLog) << "model input is not a float square [1, 3, side, side]:"
                                               << input.shape;
            return false;
        }
        session->inputSize = static_cast<int>(input.shape[2]);
        session->tilePx = qRound(session->inputSize * kTileToInput);

        if (yolo) {
            // The anchor loop and the class count are sized from this shape, so an export that does
            // not state them concretely is refused rather than guessed at.
            const TensorSpec output = specOf(session->session->GetOutputTypeInfo(0));
            if ((output.type != kFloat) || (output.shape.size() != 3) || !dimIsOneOrDynamic(output.shape[0])
                || (output.shape[1] < 5) || (output.shape[2] < 1)) {
                qCWarning(PersonDetectorWorkerLog) << "model output is not a float [1, 4 + classes, anchors]:"
                                                   << output.shape;
                return false;
            }
            session->numClasses = static_cast<int>(output.shape[1]) - 4;
            session->numAnchors = static_cast<int>(output.shape[2]);
            if (!PersonDetectorCore::classIdsWithin(*descriptor, session->numClasses)) {
                qCWarning(PersonDetectorWorkerLog) << "descriptor names a class id outside the model's"
                                                   << session->numClasses << "classes";
                return false;
            }
        } else {
            // mmdeploy decides the detection count per frame and names the row-width axis
            // symbolically, so both arrive dynamic; the tensor that actually comes back is measured
            // against kDetFields per row before it is read. The labels are the model's own ids and
            // are checked against nothing: one the descriptor does not name is simply not counted.
            const TensorSpec dets = specOf(session->session->GetOutputTypeInfo(0));
            const bool detRowOk = (dets.shape.size() == 3)
                                  && ((dets.shape[2] < 0) || (dets.shape[2] == PersonDetectorCore::kDetFields));
            if ((dets.type != kFloat) || !detRowOk || !dimIsOneOrDynamic(dets.shape[0])
                || !dimIsCountOrDynamic(dets.shape[1])) {
                qCWarning(PersonDetectorWorkerLog) << "first model output is not a float [1, dets, 5]:" << dets.shape;
                return false;
            }
            const TensorSpec labels = specOf(session->session->GetOutputTypeInfo(1));
            if ((labels.type != kInt64) || (labels.shape.size() != 2) || !dimIsOneOrDynamic(labels.shape[0])
                || !dimIsCountOrDynamic(labels.shape[1])) {
                qCWarning(PersonDetectorWorkerLog) << "second model output is not an int64 [1, dets]:" << labels.shape;
                return false;
            }
        }
        session->descriptor = *descriptor;

        qCDebug(PersonDetectorWorkerLog) << "model" << descriptor->name
                                         << "layout" << (yolo ? "yolo" : "detsLabels")
                                         << "input" << session->inputSize
                                         << "classes" << session->numClasses
                                         << "anchors" << session->numAnchors
                                         << "tile" << session->tilePx;
        _session = std::move(session);
    } catch (const std::exception& e) {
        qCWarning(PersonDetectorWorkerLog) << "session creation failed:" << e.what();
        return false;
    }

    return true;
}

bool PersonDetectorWorker::detectsVehicles() const
{
    return _session && !_session->descriptor.vehicleClasses.isEmpty();
}

PersonDetectorWorker::Detections PersonDetectorWorker::detect(const QImage& frame, int* inferenceMs)
{
    if (inferenceMs) {
        *inferenceMs = 0;
    }
    if (!_session || frame.isNull()) {
        return {};
    }

    // Below a couple of tiles the frame is small enough that the whole picture pass covers it.
    const int tilePx = _session->tilePx;
    const bool tiled = (frame.width() >= (2 * tilePx)) && (frame.height() >= (2 * tilePx));
    const std::vector<int> xs = tiled ? tileOrigins(frame.width(), tilePx) : std::vector<int>{};
    const std::vector<int> ys = tiled ? tileOrigins(frame.height(), tilePx) : std::vector<int>{};
    // Region 0 is the whole picture, the only one that sees a subject too big for a tile.
    const int regionCount = qMin(kMaxRegions, 1 + static_cast<int>(xs.size() * ys.size()));

    // A sweep holds results from several seconds ago, which are only the same scene while the
    // camera is still. A slew or a zoom step changes the picture wholesale, and the cheapest
    // way to see that is to compare a thumbnail of it.
    const QImage print = frame.scaled(16, 9, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_Grayscale8);
    bool sceneChanged = (_scenePrint.size() != print.size());
    if (!sceneChanged) {
        int diff = 0;
        for (int y = 0; y < print.height(); ++y) {
            const uchar* const now = print.constScanLine(y);
            const uchar* const before = _scenePrint.constScanLine(y);
            for (int x = 0; x < print.width(); ++x) {
                diff += qAbs(int(now[x]) - int(before[x]));
            }
        }
        sceneChanged = (diff / (print.width() * print.height())) > 12;
    }
    _scenePrint = print;

    if ((frame.size() != _lastFrameSize) || (_regions.size() != regionCount) || sceneChanged) {
        _lastFrameSize = frame.size();
        _regions.assign(regionCount, Detections());
        _regionCursor = 0;
        qCDebug(PersonDetectorWorkerLog) << "sweep reset" << _lastFrameSize << "regions" << regionCount
                                         << (sceneChanged ? "scene changed" : "");
    }

    QElapsedTimer timer;
    timer.start();

    // One region per frame rather than the whole sweep at once: a sweep is dozens of
    // inferences and would hold the picture for seconds, while this keeps every frame's cost
    // to one inference and still covers the picture, a region at a time.
    const int index = _regionCursor;
    _regionCursor = (_regionCursor + 1) % regionCount;

    if (index == 0) {
        _regions[0] = _detectRegion(frame);
    } else {
        const int tileIndex = index - 1;
        const QRect tile(xs[tileIndex % xs.size()], ys[tileIndex / xs.size()], tilePx, tilePx);
        Detections found = _detectRegion(frame.copy(tile));
        PersonDetectorCore::tileBoxesToFrame(found.persons, tile, frame.size());
        PersonDetectorCore::tileBoxesToFrame(found.vehicles, tile, frame.size());
        _regions[index] = found;
    }

    if (inferenceMs) {
        *inferenceMs = static_cast<int>(timer.elapsed());
    }

    // Everything the sweep is holding, deduplicated: a person in the overlap between two tiles
    // is found by both, and by the whole frame pass when they are large enough.
    Detections merged;
    for (const Detections& region : _regions) {
        merged.persons.append(region.persons);
        merged.vehicles.append(region.vehicles);
    }
    merged.persons = PersonDetectorCore::mergeBoxes(merged.persons, kMergeIou);
    merged.vehicles = PersonDetectorCore::mergeBoxes(merged.vehicles, kMergeIou);
    return merged;
}

PersonDetectorWorker::Detections PersonDetectorWorker::_detectRegion(const QImage& frame)
{
    if (!_session || frame.isNull()) {
        return {};
    }

    const int side = _session->inputSize;
    const int plane = side * side;
    const PersonDetectorCore::Letterbox lb = PersonDetectorCore::letterbox(frame, side);
    _input.resize(3 * plane);
    PersonDetectorCore::fillInput(lb, _session->descriptor.preprocess, _input);

    try {
        const std::array<int64_t, 4> shape = {1, 3, side, side};
        Ort::Value input = Ort::Value::CreateTensor<float>(_session->memoryInfo, _input.data(), _input.size(),
                                                          shape.data(), shape.size());
        const char* const inputNames[] = {_session->inputName.c_str()};
        std::vector<const char*> outputNames;
        outputNames.reserve(_session->outputNames.size());
        for (const std::string& name : _session->outputNames) {
            outputNames.push_back(name.c_str());
        }
        const std::vector<Ort::Value> outputs = _session->session->Run(Ort::RunOptions{nullptr}, inputNames, &input, 1,
                                                                      outputNames.data(), outputNames.size());

        if (outputs.size() != outputNames.size()) {
            qCWarning(PersonDetectorWorkerLog) << "unexpected model output";
            return {};
        }
        const PersonDetectorCore::ModelDescriptor& model = _session->descriptor;

        if (model.layout == PersonDetectorCore::OutputLayout::Yolo) {
            // load() vetted the shape the model declares; this vets the tensor that actually came
            // back, since decodeBoxes indexes it by the anchor and class counts from that declaration.
            const size_t expected = static_cast<size_t>(4 + _session->numClasses) * _session->numAnchors;
            if (!tensorIs(outputs[0], ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                || (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() != expected)) {
                qCWarning(PersonDetectorWorkerLog) << "unexpected model output size or type";
                return {};
            }
            const float* const tensor = outputs[0].GetTensorData<float>();
            return {PersonDetectorCore::decodeBoxes(tensor, _session->numAnchors, lb, frame.size(),
                                                    model.personClasses, model.confThreshold, kTileNmsIou),
                    PersonDetectorCore::decodeBoxes(tensor, _session->numAnchors, lb, frame.size(),
                                                    model.vehicleClasses, model.confThreshold, kTileNmsIou)};
        }

        // The model kept whichever detections survived its own NMS, so the count comes from the
        // tensors rather than from anything load() could size.
        if (!tensorIs(outputs[0], ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            || !tensorIs(outputs[1], ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)) {
            qCWarning(PersonDetectorWorkerLog) << "unexpected model output type";
            return {};
        }
        const size_t numDets = outputs[1].GetTensorTypeAndShapeInfo().GetElementCount();
        const size_t detElements = numDets * PersonDetectorCore::kDetFields;
        if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() != detElements) {
            qCWarning(PersonDetectorWorkerLog) << "unexpected model output size";
            return {};
        }
        const std::span<const float> dets(outputs[0].GetTensorData<float>(), detElements);
        const std::span<const int64_t> labels(outputs[1].GetTensorData<int64_t>(), numDets);
        return {PersonDetectorCore::decodeDetsLabels(dets, labels, lb, frame.size(), model.personClasses,
                                                     model.confThreshold),
                PersonDetectorCore::decodeDetsLabels(dets, labels, lb, frame.size(), model.vehicleClasses,
                                                     model.confThreshold)};
    } catch (const std::exception& e) {
        qCWarning(PersonDetectorWorkerLog) << "inference failed:" << e.what();
        return {};
    }
}

#else  // QGC_HAS_ONNXRUNTIME

struct PersonDetectorWorker::Session
{
};

PersonDetectorWorker::PersonDetectorWorker(QObject* parent)
    : QObject(parent)
{
}

PersonDetectorWorker::~PersonDetectorWorker() = default;

bool PersonDetectorWorker::load()
{
    qCWarning(PersonDetectorWorkerLog) << "built without ONNX Runtime";
    return false;
}

bool PersonDetectorWorker::detectsVehicles() const
{
    return false;
}

PersonDetectorWorker::Detections PersonDetectorWorker::detect(const QImage& frame, int* inferenceMs)
{
    Q_UNUSED(frame);
    if (inferenceMs) {
        *inferenceMs = 0;
    }
    return {};
}

PersonDetectorWorker::Detections PersonDetectorWorker::_detectRegion(const QImage& frame)
{
    Q_UNUSED(frame);
    return {};
}

#endif  // QGC_HAS_ONNXRUNTIME
