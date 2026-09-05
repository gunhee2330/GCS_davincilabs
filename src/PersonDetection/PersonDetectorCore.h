#pragma once

#include <QtCore/QList>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtGui/QImage>

/// Pure-logic half of the person detector: YOLOv8 letterbox in, decoded person boxes out.
/// No ONNX Runtime here so it unit-tests on every build.
namespace PersonDetectorCore {

constexpr int kInputSize = 320;    ///< YOLOv8n export size (square)
constexpr int kPersonClass = 0;    ///< COCO "person"
constexpr int kNumClasses = 80;
constexpr int kNumAnchors = 2100;  ///< 40*40 + 20*20 + 10*10 for a 320 input

struct Letterbox
{
    QImage image;     ///< kInputSize x kInputSize RGB888, grey padded
    float scale = 1;  ///< source px -> input px
    int padX = 0;     ///< left pad in input px
    int padY = 0;     ///< top pad in input px
};

Letterbox letterbox(const QImage& source);

/// output is the raw YOLOv8 tensor [4 + kNumClasses][kNumAnchors] (row-major floats, dequantised),
/// cx/cy/w/h in input pixels (stock Ultralytics export). Returns person boxes normalised 0..1 in source image space.
QList<QRectF> decodePersons(const float* output, const Letterbox& lb, const QSize& sourceSize,
                            float confThreshold = 0.4f, float iouThreshold = 0.45f);

}  // namespace PersonDetectorCore
