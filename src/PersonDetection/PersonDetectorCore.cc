#include "PersonDetectorCore.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtGui/QPainter>

#include <algorithm>
#include <array>
#include <vector>

namespace PersonDetectorCore {

std::optional<ModelDescriptor> parseModelDescriptor(const QByteArray& json, QString* error)
{
    const auto fail = [error](const QString& reason) {
        if (error) {
            *error = reason;
        }
        return std::optional<ModelDescriptor>();
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        return fail(QStringLiteral("not a JSON object: %1").arg(parseError.errorString()));
    }
    const QJsonObject object = doc.object();

    ModelDescriptor descriptor;
    descriptor.name = object.value(QStringLiteral("name")).toString();

    const QString layout = object.value(QStringLiteral("layout")).toString();
    if (layout == QLatin1StringView("yolo")) {
        descriptor.layout = OutputLayout::Yolo;
    } else if (layout == QLatin1StringView("detsLabels")) {
        descriptor.layout = OutputLayout::DetsLabels;
    } else {
        return fail(QStringLiteral("\"layout\" is not \"yolo\" or \"detsLabels\""));
    }

    const QString preprocess = object.value(QStringLiteral("preprocess")).toString();
    if (preprocess == QLatin1StringView("rgb01")) {
        descriptor.preprocess = Preprocess::Rgb01;
    } else if (preprocess == QLatin1StringView("bgrMeanStd")) {
        descriptor.preprocess = Preprocess::BgrMeanStd;
    } else {
        return fail(QStringLiteral("\"preprocess\" is not \"rgb01\" or \"bgrMeanStd\""));
    }

    QString idError;
    const auto readIds = [&](const QString& key, QList<int>& into) {
        if (!idError.isEmpty()) {
            return;
        }
        const QJsonValue value = object.value(key);
        if (!value.isArray()) {
            idError = QStringLiteral("\"%1\" is missing or not an array").arg(key);
            return;
        }
        const QJsonArray ids = value.toArray();
        for (const QJsonValue& id : ids) {
            // toInt's default also catches a non-integral or non-numeric entry, which the test
            // below then rejects along with a negative id.
            const int classId = id.toInt(-1);
            if (classId < 0) {
                idError = QStringLiteral("\"%1\" holds an entry that is not a class id").arg(key);
                return;
            }
            into.append(classId);
        }
    };
    readIds(QStringLiteral("person"), descriptor.personClasses);
    readIds(QStringLiteral("vehicle"), descriptor.vehicleClasses);
    if (!idError.isEmpty()) {
        return fail(idError);
    }
    // An empty vehicle list is a model that was never trained on them; an empty person list would
    // leave the head count reading zero for every frame, which is a broken descriptor, not a choice.
    if (descriptor.personClasses.isEmpty()) {
        return fail(QStringLiteral("\"person\" is empty, nothing would be counted"));
    }

    const QJsonValue conf = object.value(QStringLiteral("conf"));
    if (!conf.isDouble() || (conf.toDouble() <= 0.0) || (conf.toDouble() > 1.0)) {
        return fail(QStringLiteral("\"conf\" is not a threshold in (0, 1]"));
    }
    descriptor.confThreshold = static_cast<float>(conf.toDouble());

    return descriptor;
}

bool classIdsWithin(const ModelDescriptor& descriptor, int numClasses)
{
    const auto exists = [numClasses](int classId) { return (classId >= 0) && (classId < numClasses); };
    return std::ranges::all_of(descriptor.personClasses, exists)
           && std::ranges::all_of(descriptor.vehicleClasses, exists);
}

Letterbox letterbox(const QImage& source, int inputSize)
{
    Letterbox lb;
    lb.image = QImage(inputSize, inputSize, QImage::Format_RGB888);
    lb.image.fill(QColor(114, 114, 114));
    if (source.isNull()) {
        return lb;
    }
    lb.scale = std::min(float(inputSize) / source.width(), float(inputSize) / source.height());
    const int w = qRound(source.width() * lb.scale);
    const int h = qRound(source.height() * lb.scale);
    lb.padX = (inputSize - w) / 2;
    lb.padY = (inputSize - h) / 2;
    QPainter painter(&lb.image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(lb.padX, lb.padY, w, h), source);
    return lb;
}

void fillInput(const Letterbox& lb, Preprocess preprocess, std::span<float> input)
{
    const int side = lb.image.width();
    const size_t plane = static_cast<size_t>(side) * side;
    // Read a scanline at a time below, so anything but the letterbox's own format or size would be
    // read past the end of a row rather than rejected by the indexing.
    if ((lb.image.format() != QImage::Format_RGB888) || (lb.image.height() != side)
        || (input.size() != (3 * plane))) {
        return;
    }

    // mmdet's RTMDet takes BGR planes shifted by the ImageNet mean and divided by its std, and this
    // export does not fold that into its first convolution; an Ultralytics export takes RGB in 0..1.
    const bool bgr = (preprocess == Preprocess::BgrMeanStd);
    const std::array<float, 3> mean = bgr ? std::array<float, 3>{103.53f, 116.28f, 123.675f}
                                          : std::array<float, 3>{0.f, 0.f, 0.f};
    const std::array<float, 3> invStd = bgr ? std::array<float, 3>{1 / 57.375f, 1 / 57.12f, 1 / 58.395f}
                                            : std::array<float, 3>{1 / 255.f, 1 / 255.f, 1 / 255.f};
    for (int y = 0; y < side; ++y) {
        const uchar* const row = lb.image.constScanLine(y);
        for (int x = 0; x < side; ++x) {
            const size_t pixel = (static_cast<size_t>(y) * side) + x;
            for (int c = 0; c < 3; ++c) {
                const uchar value = row[(3 * x) + (bgr ? (2 - c) : c)];  // RGB888 byte feeding plane c
                input[(c * plane) + pixel] = (value - mean[c]) * invStd[c];
            }
        }
    }
}

namespace {

struct Candidate
{
    QRectF box;
    float score;
};

float iou(const QRectF& a, const QRectF& b)
{
    const QRectF inter = a.intersected(b);
    const float interArea = inter.width() * inter.height();
    const float unionArea = a.width() * a.height() + b.width() * b.height() - interArea;
    return unionArea > 0 ? interArea / unionArea : 0.f;
}

/// How much of the smaller box the two share. A tile that saw only a person's shoulders
/// reports a box inside the whole frame pass's box: they barely share a union, so IoU calls
/// them different people, while this calls them one.
float containment(const QRectF& a, const QRectF& b)
{
    const QRectF inter = a.intersected(b);
    const float interArea = inter.width() * inter.height();
    const float smaller = std::min(a.width() * a.height(), b.width() * b.height());
    return smaller > 0 ? interArea / smaller : 0.f;
}

}  // namespace

QList<QRectF> decodeBoxes(const float* output, int numAnchors, const Letterbox& lb, const QSize& sourceSize,
                          std::span<const int> classIds, float confThreshold, float iouThreshold)
{
    if (!output || sourceSize.isEmpty() || lb.scale <= 0) {
        return {};
    }
    std::vector<Candidate> candidates;
    const QRectF sourceRect(QPointF(0, 0), sourceSize);
    for (int i = 0; i < numAnchors; ++i) {
        float score = 0.f;
        for (const int classId : classIds) {
            score = std::max(score, output[(4 + classId) * numAnchors + i]);
        }
        if (score < confThreshold) {
            continue;
        }
        const float cx = output[0 * numAnchors + i];
        const float cy = output[1 * numAnchors + i];
        const float w = output[2 * numAnchors + i];
        const float h = output[3 * numAnchors + i];
        QRectF box((cx - w / 2 - lb.padX) / lb.scale, (cy - h / 2 - lb.padY) / lb.scale, w / lb.scale, h / lb.scale);
        box = box.intersected(sourceRect);
        if (box.isEmpty()) {
            continue;
        }
        candidates.push_back({QRectF(box.x() / sourceSize.width(), box.y() / sourceSize.height(),
                                     box.width() / sourceSize.width(), box.height() / sourceSize.height()),
                              score});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    QList<QRectF> kept;
    // Greedy O(n^2) NMS: n is the handful of boxes above threshold, never every anchor
    for (const Candidate& c : candidates) {
        const bool overlaps =
            std::any_of(kept.cbegin(), kept.cend(), [&](const QRectF& k) { return iou(k, c.box) > iouThreshold; });
        if (!overlaps) {
            kept.append(c.box);
        }
    }
    return kept;
}

QList<QRectF> decodeDetsLabels(std::span<const float> dets, std::span<const int64_t> labels, const Letterbox& lb,
                               const QSize& sourceSize, std::span<const int> classIds, float confThreshold)
{
    if (sourceSize.isEmpty() || (lb.scale <= 0) || (dets.size() != (kDetFields * labels.size()))) {
        return {};
    }
    QList<QRectF> kept;
    const QRectF sourceRect(QPointF(0, 0), sourceSize);
    for (size_t i = 0; i < labels.size(); ++i) {
        const std::span<const float> row = dets.subspan(i * kDetFields, kDetFields);
        if (row[4] < confThreshold) {
            continue;
        }
        // A label the descriptor does not name is another of the model's classes, not this group's.
        if (std::ranges::find(classIds, labels[i]) == classIds.end()) {
            continue;
        }
        QRectF box(QPointF((row[0] - lb.padX) / lb.scale, (row[1] - lb.padY) / lb.scale),
                   QPointF((row[2] - lb.padX) / lb.scale, (row[3] - lb.padY) / lb.scale));
        box = box.intersected(sourceRect);
        if (box.isEmpty()) {
            continue;
        }
        kept.append(QRectF(box.x() / sourceSize.width(), box.y() / sourceSize.height(),
                           box.width() / sourceSize.width(), box.height() / sourceSize.height()));
    }
    return kept;
}

void tileBoxesToFrame(QList<QRectF>& boxes, const QRect& tile, const QSize& frameSize)
{
    if (frameSize.isEmpty()) {
        return;
    }
    for (QRectF& box : boxes) {
        box = QRectF((tile.x() + (box.x() * tile.width())) / frameSize.width(),
                     (tile.y() + (box.y() * tile.height())) / frameSize.height(),
                     box.width() * tile.width() / frameSize.width(),
                     box.height() * tile.height() / frameSize.height());
    }
}

QList<QRectF> mergeBoxes(const QList<QRectF>& boxes, float iouThreshold)
{
    QList<QRectF> kept;
    for (const QRectF& box : boxes) {
        if (box.isEmpty()) {
            continue;
        }
        const bool duplicate = std::any_of(kept.cbegin(), kept.cend(), [&](const QRectF& k) {
            if (iou(k, box) > iouThreshold) {
                return true;
            }
            // A fragment sits almost entirely inside the whole box and is much smaller than it.
            // Without the size test this also swallows the person standing behind another,
            // whose box overlaps just as deeply but is the same size — measured on an overhead
            // crowd, that cost 31 real people out of 122.
            const qreal areaK = k.width() * k.height();
            const qreal areaBox = box.width() * box.height();
            return (containment(k, box) > 0.8f) && (std::min(areaK, areaBox) < (0.5 * std::max(areaK, areaBox)));
        });
        if (!duplicate) {
            kept.append(box);
        }
    }
    return kept;
}

}  // namespace PersonDetectorCore
