#include "PersonDetectorCore.h"

#include <QtGui/QPainter>

#include <algorithm>
#include <vector>

namespace PersonDetectorCore {

Letterbox letterbox(const QImage& source)
{
    Letterbox lb;
    lb.image = QImage(kInputSize, kInputSize, QImage::Format_RGB888);
    lb.image.fill(QColor(114, 114, 114));
    if (source.isNull()) {
        return lb;
    }
    lb.scale = std::min(float(kInputSize) / source.width(), float(kInputSize) / source.height());
    const int w = qRound(source.width() * lb.scale);
    const int h = qRound(source.height() * lb.scale);
    lb.padX = (kInputSize - w) / 2;
    lb.padY = (kInputSize - h) / 2;
    QPainter painter(&lb.image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(lb.padX, lb.padY, w, h), source);
    return lb;
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

QList<QRectF> decodeBoxes(const float* output, const Letterbox& lb, const QSize& sourceSize,
                          std::span<const int> classIds, float confThreshold, float iouThreshold)
{
    if (!output || sourceSize.isEmpty() || lb.scale <= 0) {
        return {};
    }
    std::vector<Candidate> candidates;
    const QRectF sourceRect(QPointF(0, 0), sourceSize);
    for (int i = 0; i < kNumAnchors; ++i) {
        float score = 0.f;
        for (const int classId : classIds) {
            score = std::max(score, output[(4 + classId) * kNumAnchors + i]);
        }
        if (score < confThreshold) {
            continue;
        }
        const float cx = output[0 * kNumAnchors + i];
        const float cy = output[1 * kNumAnchors + i];
        const float w = output[2 * kNumAnchors + i];
        const float h = output[3 * kNumAnchors + i];
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
    // Greedy O(n^2) NMS: n is the handful of boxes above threshold, never all 2100
    for (const Candidate& c : candidates) {
        const bool overlaps =
            std::any_of(kept.cbegin(), kept.cend(), [&](const QRectF& k) { return iou(k, c.box) > iouThreshold; });
        if (!overlaps) {
            kept.append(c.box);
        }
    }
    return kept;
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
