#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QRect>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtGui/QImage>

#include <cstdint>
#include <optional>
#include <span>

/// Pure-logic half of the person detector: letterbox in, decoded person and vehicle boxes out.
/// No ONNX Runtime here so it unit-tests on every build. The model's shape is not stated here either:
/// it is read from the session at load time, and the class mapping from the descriptor below.
namespace PersonDetectorCore {

/// How the model states what it found. Which one a model speaks is declared in its descriptor, not
/// inferred from its outputs: two layouts that happen to have the same output count would decode
/// each other's numbers into plausible-looking boxes with nothing to flag it.
enum class OutputLayout
{
    Yolo,        ///< One output [1][4 + classes][anchors], scores per class, NMS still to do here
    DetsLabels,  ///< 'dets' [1][n][5] and 'labels' [1][n], NMS already applied inside the model
};

/// Floats per 'dets' row: x1, y1, x2, y2, score.
constexpr int kDetFields = 5;

/// What the model was trained to be fed. Declared in the descriptor for the same reason the layout
/// is: the wrong recipe does not fail, it returns thousands of confident boxes over noise.
enum class Preprocess
{
    Rgb01,       ///< RGB, 0..1, as an Ultralytics export expects
    BgrMeanStd,  ///< BGR 0..255 less mmdet's ImageNet mean, over its std, as an mmdeploy export expects
};

/// Which of the model's own class ids count as what, and the threshold to count them at. Ships as
/// JSON beside the model, so retraining on another dataset is a file swap rather than a code change.
struct ModelDescriptor
{
    QString name;
    OutputLayout layout = OutputLayout::Yolo;
    Preprocess preprocess = Preprocess::Rgb01;
    QList<int> personClasses;
    QList<int> vehicleClasses;
    /// Deliberately below the usual 0.4: a person seen from the air is a head and shoulders with no
    /// legs to help the model, and scores accordingly. The figure is the model's own, so it belongs
    /// in the descriptor beside the weights that were measured with it.
    float confThreshold = 0.3f;
};

/// Returns nullopt on any malformed field, with @a error naming it. Class ids are only checked for
/// being non-negative here: what makes an id valid is the model's own class count, which the Yolo
/// layout states in its output shape and the DetsLabels layout does not state at all.
std::optional<ModelDescriptor> parseModelDescriptor(const QByteArray& json, QString* error = nullptr);

/// True when every id @a descriptor names exists in a model of @a numClasses classes. An id outside
/// it means the descriptor was written for a different model, so the loader rejects the pair rather
/// than counting whichever class happens to sit at that index.
bool classIdsWithin(const ModelDescriptor& descriptor, int numClasses);

struct Letterbox
{
    QImage image;     ///< inputSize x inputSize RGB888, grey padded
    float scale = 1;  ///< source px -> input px
    int padX = 0;     ///< left pad in input px
    int padY = 0;     ///< top pad in input px
};

/// @a inputSize is the square side of the model's own input, from its input shape.
Letterbox letterbox(const QImage& source, int inputSize);

/// Writes @a lb's image into @a input as the NCHW planes @a preprocess calls for: 3 * side * side
/// floats, side being the letterbox's own. Nothing is written when @a input is any other size.
/// Lives here rather than at its one call site because the wrong recipe does not fail, it returns
/// thousands of confident boxes over noise, so the planes are worth pinning in a test.
void fillInput(const Letterbox& lb, Preprocess preprocess, std::span<float> input);

/// output is the raw YOLOv8 tensor [4 + classes][@a numAnchors] (row-major floats, dequantised),
/// cx/cy/w/h in input pixels (stock Ultralytics export). An anchor scores as the best of @a classIds, so one
/// class group decodes as one target. Returns boxes normalised 0..1 in source image space, best score first.
QList<QRectF> decodeBoxes(const float* output, int numAnchors, const Letterbox& lb, const QSize& sourceSize,
                          std::span<const int> classIds, float confThreshold = 0.3f, float iouThreshold = 0.45f);

/// @a dets is the 'dets' tensor, kDetFields floats per detection (x1, y1, x2, y2 in letterboxed
/// input pixels, then the score), and @a labels the model's own class id for each of them. The model
/// has already suppressed overlaps, so nothing is dropped here beyond the confidence threshold and
/// labels outside @a classIds. Order is kept: the export sorts by score. Returns boxes normalised
/// 0..1 in source image space, as decodeBoxes does.
QList<QRectF> decodeDetsLabels(std::span<const float> dets, std::span<const int64_t> labels, const Letterbox& lb,
                               const QSize& sourceSize, std::span<const int> classIds, float confThreshold = 0.3f);

/// Maps @a boxes, normalised 0..1 inside @a tile, into 0..1 in a frame of @a frameSize. A tile pass
/// letterboxes its own crop, so what comes back is in the crop's coordinates. Lives here for the
/// reason fillInput does: losing the tile's origin leaves every box inside 0..1 and inside the
/// frame, only piled into its top-left corner, which no count or range check downstream can see.
void tileBoxesToFrame(QList<QRectF>& boxes, const QRect& tile, const QSize& frameSize);

/// Drops boxes that duplicate an earlier one, which is what the same person looks like when
/// two passes over the same frame both find them. Order is kept, so pass the more trustworthy
/// list first. Boxes are normalised 0..1, as decodeBoxes returns them.
QList<QRectF> mergeBoxes(const QList<QRectF>& boxes, float iouThreshold = 0.45f);

}  // namespace PersonDetectorCore
