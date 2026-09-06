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
#endif

QGC_LOGGING_CATEGORY(PersonDetectorWorkerLog, "PersonDetection.PersonDetectorWorker")

namespace {

/// Device tuning knob: 4 matches the tablet's big-core cluster. Raise or lower per device.
constexpr int kThreads = 4;

/// The frame is covered in tiles smaller than the model's own input, so a person is enlarged
/// on the way in rather than shrunk with the rest of the frame: at 1280x720 a whole frame pass
/// squeezes a 30 px person down to 8 px and finds nothing, while a 192 px tile hands the model
/// a 50 px one. Tiles overlap because a person cut by a tile edge scores as neither half.
/// Measured on one overhead crowd: whole frame 0, 320 px tiles 47, 192 px tiles 65, and 192 px
/// tiles with this overlap 122. Smaller and denser keeps helping, at a tile per inference.
constexpr int kTilePx = 192;
constexpr qreal kTileOverlap = 0.25;

/// Crowds really do overlap, so suppression inside a tile is looser than the usual 0.45, and
/// the merge across tiles looser still — the same person seen by two tiles is one person.
constexpr float kTileNmsIou = 0.6f;
constexpr float kMergeIou = 0.55f;

/// A sweep is one inference per region, so this is also its length: 64 regions is about eight
/// seconds on the tablet, which is as stale as a bracket may get.
constexpr int kMaxRegions = 64;

/// Below this the frame is already smaller than a couple of tiles and one pass covers it.
constexpr int kMinTiledSide = kTilePx * 2;

/// Tile origins along one axis: stepped by the overlap, with the last one pulled back to the
/// edge so the far side is covered too.
std::vector<int> tileOrigins(int length)
{
    std::vector<int> origins;
    const int step = qMax(1, qRound(kTilePx * (1.0 - kTileOverlap)));
    for (int at = 0; (at + kTilePx) <= length; at += step) {
        origins.push_back(at);
    }
    if (origins.empty()) {
        origins.push_back(0);
    } else if (origins.back() + kTilePx < length) {
        origins.push_back(length - kTilePx);
    }
    return origins;
}

constexpr const char* kModelResource = ":/PersonDetection/yolov8n_320.onnx";

}  // namespace

#ifdef QGC_HAS_ONNXRUNTIME

namespace {

/// One environment per process, as ONNX Runtime expects.
Ort::Env& ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "QGCPersonDetector");
    return env;
}

}  // namespace

struct PersonDetectorWorker::Session
{
    Ort::SessionOptions options;
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::unique_ptr<Ort::Session> session;
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

    QFile modelFile(QString::fromLatin1(kModelResource));
    if (!modelFile.open(QIODevice::ReadOnly)) {
        qCWarning(PersonDetectorWorkerLog) << "model resource unavailable:" << kModelResource;
        return false;
    }
    const QByteArray model = modelFile.readAll();

    try {
        auto session = std::make_unique<Session>();
        session->options.SetIntraOpNumThreads(kThreads);
        session->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session->session = std::make_unique<Ort::Session>(ortEnv(), model.constData(),
                                                         static_cast<size_t>(model.size()), session->options);
        _session = std::move(session);
    } catch (const std::exception& e) {
        qCWarning(PersonDetectorWorkerLog) << "session creation failed:" << e.what();
        return false;
    }

    return true;
}

PersonDetectorWorker::Detections PersonDetectorWorker::detect(const QImage& frame, int* inferenceMs)
{
    if (inferenceMs) {
        *inferenceMs = 0;
    }
    if (!_session || frame.isNull()) {
        return {};
    }

    const bool tiled = (frame.width() >= kMinTiledSide) && (frame.height() >= kMinTiledSide);
    const std::vector<int> xs = tiled ? tileOrigins(frame.width()) : std::vector<int>{};
    const std::vector<int> ys = tiled ? tileOrigins(frame.height()) : std::vector<int>{};
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
        const QRect tile(xs[tileIndex % xs.size()], ys[tileIndex / xs.size()], kTilePx, kTilePx);
        Detections found = _detectRegion(frame.copy(tile));
        const auto toFrame = [&](QList<QRectF>& boxes) {
            for (QRectF& box : boxes) {
                box = QRectF((tile.x() + (box.x() * tile.width())) / frame.width(),
                             (tile.y() + (box.y() * tile.height())) / frame.height(),
                             box.width() * tile.width() / frame.width(),
                             box.height() * tile.height() / frame.height());
            }
        };
        toFrame(found.persons);
        toFrame(found.vehicles);
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

    constexpr int side = PersonDetectorCore::kInputSize;
    constexpr int plane = side * side;
    const PersonDetectorCore::Letterbox lb = PersonDetectorCore::letterbox(frame);
    _input.resize(3 * plane);
    for (int y = 0; y < side; ++y) {
        const uchar* const row = lb.image.constScanLine(y);
        for (int x = 0; x < side; ++x) {
            const int pixel = (y * side) + x;
            _input[pixel] = row[3 * x] / 255.f;
            _input[plane + pixel] = row[(3 * x) + 1] / 255.f;
            _input[(2 * plane) + pixel] = row[(3 * x) + 2] / 255.f;
        }
    }

    try {
        const std::array<int64_t, 4> shape = {1, 3, side, side};
        Ort::Value input = Ort::Value::CreateTensor<float>(_session->memoryInfo, _input.data(), _input.size(),
                                                          shape.data(), shape.size());
        static constexpr const char* inputNames[] = {"images"};
        static constexpr const char* outputNames[] = {"output0"};
        const std::vector<Ort::Value> outputs =
            _session->session->Run(Ort::RunOptions{nullptr}, inputNames, &input, 1, outputNames, 1);

        if ((outputs.size() != 1) || !outputs[0].IsTensor()) {
            qCWarning(PersonDetectorWorkerLog) << "unexpected model output";
            return {};
        }
        // Guards decodeBoxes against a model exported with a different head or input size.
        const std::vector<int64_t> outputShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if ((outputShape.size() != 3) || (outputShape[1] != (4 + PersonDetectorCore::kNumClasses))
            || (outputShape[2] != PersonDetectorCore::kNumAnchors)) {
            qCWarning(PersonDetectorWorkerLog) << "unexpected model output shape";
            return {};
        }

        const float* const tensor = outputs[0].GetTensorData<float>();
        constexpr float conf = 0.3f;
        return {PersonDetectorCore::decodeBoxes(tensor, lb, frame.size(), PersonDetectorCore::kPersonClasses, conf,
                                                kTileNmsIou),
                PersonDetectorCore::decodeBoxes(tensor, lb, frame.size(), PersonDetectorCore::kVehicleClasses, conf,
                                                kTileNmsIou)};
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
