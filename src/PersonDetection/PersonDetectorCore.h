#pragma once

#include <QtCore/QList>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtGui/QImage>

#include <array>
#include <span>

/// Pure-logic half of the person detector: YOLOv8 letterbox in, decoded person and vehicle boxes out.
/// No ONNX Runtime here so it unit-tests on every build.
namespace PersonDetectorCore {

constexpr int kInputSize = 320;                         ///< YOLOv8n export size (square)
constexpr int kNumClasses = 80;
constexpr int kNumAnchors = 2100;                       ///< 40*40 + 20*20 + 10*10 for a 320 input

constexpr std::array<int, 1> kPersonClasses{0};         ///< COCO "person"
constexpr std::array<int, 3> kVehicleClasses{2, 5, 7};  ///< COCO "car", "bus", "truck"

struct Letterbox
{
    QImage image;     ///< kInputSize x kInputSize RGB888, grey padded
    float scale = 1;  ///< source px -> input px
    int padX = 0;     ///< left pad in input px
    int padY = 0;     ///< top pad in input px
};

Letterbox letterbox(const QImage& source);

/// The default threshold is deliberately below the usual 0.4: a person seen from the air is a
/// head and shoulders with no legs to help the model, and scores accordingly. Measured on an
/// overhead crowd, 0.4 found 35 and 0.3 found 47 of them.
/// output is the raw YOLOv8 tensor [4 + kNumClasses][kNumAnchors] (row-major floats, dequantised),
/// cx/cy/w/h in input pixels (stock Ultralytics export). An anchor scores as the best of @a classIds, so one
/// class group decodes as one target. Returns boxes normalised 0..1 in source image space, best score first.
QList<QRectF> decodeBoxes(const float* output, const Letterbox& lb, const QSize& sourceSize,
                          std::span<const int> classIds, float confThreshold = 0.3f, float iouThreshold = 0.45f);

/// Drops boxes that duplicate an earlier one, which is what the same person looks like when
/// two passes over the same frame both find them. Order is kept, so pass the more trustworthy
/// list first. Boxes are normalised 0..1, as decodeBoxes returns them.
QList<QRectF> mergeBoxes(const QList<QRectF>& boxes, float iouThreshold = 0.45f);

}  // namespace PersonDetectorCore
