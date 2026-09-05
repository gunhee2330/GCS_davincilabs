#include "PersonDetectorWorker.h"

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>

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

    QElapsedTimer timer;
    timer.start();

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
        const Detections detections = {
            PersonDetectorCore::decodeBoxes(tensor, lb, frame.size(), PersonDetectorCore::kPersonClasses),
            PersonDetectorCore::decodeBoxes(tensor, lb, frame.size(), PersonDetectorCore::kVehicleClasses)};
        if (inferenceMs) {
            *inferenceMs = static_cast<int>(timer.elapsed());
        }
        return detections;
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

#endif  // QGC_HAS_ONNXRUNTIME
