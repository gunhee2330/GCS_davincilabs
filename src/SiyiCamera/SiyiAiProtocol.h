#pragma once

#include <optional>

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtTypes>

/// \brief Codec for the SIYI AI Tracking Module SDK.
///
/// The module is separate hardware that sits between the optical pod and the SIYI link. It
/// reuses the packet framing of SiyiProtocol but numbers its commands independently, and it
/// answers on its own address, so it is driven as a second endpoint rather than as more
/// camera commands.
///
/// On this interface the module reports one selected target at a time and never enumerates the
/// rest of what it sees, so no headcount can be derived from it. Counts do exist, but only on the
/// module's second, undocumented link - see PrivateCommandId::ObjectCount below - and even there
/// they are per-class tallies with no boxes attached.
namespace SiyiAi {

/// Factory defaults. The camera itself lives on .25.
inline constexpr char kDefaultAddress[] = "192.168.144.60";
inline constexpr quint16 kDefaultPort = 37260;

/// Second port on the same module, TCP only, framed by SiyiLongProtocol instead of SiyiProtocol.
/// Not a replacement for kDefaultPort: the two links carry different command sets and the module
/// serves both at once. Read out of the firmware (tcp_server_init@0x55c31c) and confirmed by the
/// app's own device list (assets/siyi_camera.json, "controlUrl":"192.168.144.60:37256").
inline constexpr quint16 kPrivatePort = 37256;

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

/// Commands on the module's private link (kPrivatePort, SiyiLongProtocol framing).
///
/// Numbered independently of CommandId above and of SiyiProtocol's camera commands: 0x80 is a
/// camera command in the manual and the keep-alive here. Taken from the module's own dispatch
/// table (get_ai_tcp_server_ai_action_func@0x56e820, table 0xf898f0); no SIYI manual documents
/// this link or any command on it.
enum class PrivateCommandId : quint8 {
    /// Empty payload, no reply. Used as a keep-alive on an assumption, not a reading:
    /// tcp_server_init@0x55c31c carries what looks like a -10 second idle field, so a client that
    /// says nothing may well be hung up on. Neither that timeout nor which commands push it back
    /// was disassembled - the spec lists the idle disconnect among the things still to be measured
    /// on a bench - and the confirmed part is only that 0x80 is entry [4] of table 0xf898f0.
    KeepAlive   = 0x80,

    /// Per-class object tallies: the switch, the class list, and the unsolicited count pushes.
    ObjectCount = 0xD5,
};

/// First payload byte of a PrivateCommandId::ObjectCount request. An empty payload instead of a
/// mode byte asks for the current state.
enum class ObjectCountMode : quint8 {
    Stop      = 0,
    Start     = 1,
    SetMask   = 2,   ///< Followed by a class count and that many booleans.
    ClassList = 3,
};

/// A class tally is one unsigned byte and the firmware accumulates into it without clamping
/// (apt_push_datect_obj_statistics@0x577358/60, ldrb then strb), so the counter wraps rather than
/// sticking: a crowd of 300 arrives as 44, indistinguishable on the wire from a crowd of 44.
///
/// A tally that reaches this value therefore has to be shown as a floor rather than a number - but
/// be honest about what that catches. It catches only the one moment the wire value is exactly 255.
/// It cannot detect a wrap that has already happened, and no receiver can: the protocol carries
/// nothing to detect it with. Until the module's own per-frame object limit has been measured on a
/// bench, none of these numbers may be presented as an exact headcount.
inline constexpr int kObjectCountSaturation = 255;

/// A PrivateCommandId::ObjectCount reply or unsolicited push.
struct ObjectCountReport {
    bool counting = false;      ///< The module's counting switch, as it reports it.

    /// One tally per class, in the module's own class order - which only parseObjectClassNames
    /// can turn into meaning. Empty for the bare state reply, which carries the switch alone.
    QList<int> counts;
};

/// Reply to PrivateCommandId::ObjectCount.
[[nodiscard]] std::optional<ObjectCountReport> parseObjectCountReport(const QByteArray &data);

/// Class names from an ObjectCountMode::ClassList reply, in the module's class order.
///
/// Empty when the reply is malformed or names fewer classes than it counts. Class
/// numbering belongs to whichever model the module has loaded, so it is read rather than assumed:
/// a firmware or model change reorders it silently and this link carries no version to notice
/// that by.
[[nodiscard]] QStringList parseObjectClassNames(const QByteArray &data);

/// Commands whose payload is empty.
[[nodiscard]] QByteArray encodeRequest(CommandId commandId, quint16 sequence = 0);

[[nodiscard]] QByteArray encodeSetRecognition(bool enabled, quint16 sequence = 0);
[[nodiscard]] QByteArray encodeSetTargetStream(bool enabled, quint16 sequence = 0);

/// Opens or closes the module's RTSP video.
///
/// Only needed on a UDP or serial control link. Over TCP the module ties the stream to the
/// connection and this is never sent, which is why SIYI's own app never has to think about it -
/// and why this went unnoticed until the module was on the bench: the server completes DESCRIBE
/// whether or not the encoder is running, so the stream looks alive right up until no frame
/// arrives. Manual v1.2, CN 0x0b note 2.
[[nodiscard]] QByteArray encodeSetVideoStream(bool enabled, quint16 sequence = 0);

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
