#pragma once

#include <optional>

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QtTypes>

/// \brief Codec for the SIYI AI Tracking Module SDK.
///
/// The module is separate hardware that sits between the optical pod and the SIYI link. It
/// reuses the packet framing of SiyiProtocol but numbers its commands independently, and it
/// answers on its own address, so it is driven as a second endpoint rather than as more
/// camera commands.
///
/// The module reports one selected target at a time. It does not enumerate everything it can
/// see, so a headcount cannot be derived from this interface.
namespace SiyiAi {

/// Factory defaults. The camera itself lives on .25.
inline constexpr char kDefaultAddress[] = "192.168.144.60";
inline constexpr quint16 kDefaultPort = 37260;

/// Target coordinates are expressed against this fixed frame whatever the stream resolution.
inline constexpr int kReferenceWidth = 1280;
inline constexpr int kReferenceHeight = 720;

enum class CommandId : quint8 {
    Heartbeat                = 0x00,
    RequestFirmwareVersion   = 0x01,
    RequestRecognitionState  = 0x03,
    SetRecognitionState      = 0x04,
    RequestTrackingState     = 0x05,
    SetTrackTarget           = 0x06,
    RequestTargetStreamState = 0x08,
    SetTargetStreamState     = 0x09,
    TargetStream             = 0x0A,
    SetVideoStream           = 0x0B,
};

enum class TargetType : quint8 {
    Person    = 0,
    Car       = 1,
    Bus       = 2,
    Truck     = 3,
    Arbitrary = 255,
};

enum class TrackingStatus : quint8 {
    Tracking          = 0,
    IntermittentLoss  = 1,   ///< Briefly occluded; the module keeps following.
    Lost              = 2,
    CancelledByUser   = 3,
    TrackingArbitrary = 4,
};

/// Acknowledgement of CommandId::SetTrackTarget.
enum class TrackRequestResult : quint8 {
    Error              = 0,
    Accepted           = 1,
    NotInTrackingMode  = 2,
    StreamUnsupported  = 3,
};

/// Reply to CommandId::RequestTargetStreamState.
enum class TargetStreamState : quint8 {
    Closed         = 0,
    Streaming      = 1,
    RecognitionOff = 2,
    NoTarget       = 3,
};

/// One tracked target, from CommandId::TargetStream.
struct TrackedTarget {
    quint16 centreX = 0;    ///< Centre of the recognition box, in the reference frame.
    quint16 centreY = 0;
    quint16 width = 0;
    quint16 height = 0;
    TargetType type = TargetType::Person;
    TrackingStatus status = TrackingStatus::Lost;
};

/// Commands whose payload is empty.
[[nodiscard]] QByteArray encodeRequest(CommandId commandId, quint16 sequence = 0);

[[nodiscard]] QByteArray encodeSetRecognition(bool enabled, quint16 sequence = 0);
[[nodiscard]] QByteArray encodeSetTargetStream(bool enabled, quint16 sequence = 0);

/// Picks whatever the operator tapped. Coordinates are in the video stream's own resolution.
[[nodiscard]] QByteArray encodeTrackPoint(quint16 x, quint16 y, quint16 sequence = 0);

/// Selects a target by dragging a box, given its top-left and bottom-right corners.
[[nodiscard]] QByteArray encodeTrackBox(quint16 left, quint16 top, quint16 right, quint16 bottom, quint16 sequence = 0);

[[nodiscard]] QByteArray encodeCancelTracking(quint16 sequence = 0);

[[nodiscard]] std::optional<TrackedTarget> parseTargetStream(const QByteArray &data);

/// Single-byte replies. Empty optional when the payload is malformed.
[[nodiscard]] std::optional<bool> parseEnabledFlag(const QByteArray &data);
[[nodiscard]] std::optional<TrackRequestResult> parseTrackRequestResult(const QByteArray &data);
[[nodiscard]] std::optional<TargetStreamState> parseTargetStreamState(const QByteArray &data);

/// Human readable target class, for status readouts.
[[nodiscard]] QString targetTypeName(TargetType type);

} // namespace SiyiAi
