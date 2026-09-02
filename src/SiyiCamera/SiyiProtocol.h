#pragma once

#include <optional>

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QtTypes>

/// \brief Codec for the SIYI gimbal camera SDK protocol (A8/ZR10/ZR30/ZT6/ZT30).
///
/// Packet layout, per the "SDK Protocol Format" chapter of the SIYI camera manuals:
///
///     STX(2) | CTRL(1) | DATA_LEN(2) | SEQ(2) | CMD_ID(1) | DATA(DATA_LEN) | CRC16(2)
///
/// STX is 0x55 0x66. Multi-byte fields are little endian. DATA_LEN counts only DATA.
/// CRC16 is CRC-16/XMODEM (polynomial 0x1021, initial value 0) over every byte ahead of it.
///
/// This namespace is transport agnostic: SIYI carries the same framing over UDP, TCP and
/// TTL serial.
namespace SiyiProtocol {

inline constexpr quint8 kHeaderByte1 = 0x55;
inline constexpr quint8 kHeaderByte2 = 0x66;

/// Bytes in a packet that carries no data payload.
inline constexpr int kMinPacketSize = 10;

enum class CommandId : quint8 {
    AcquireFirmwareVersion  = 0x01,
    AcquireHardwareId       = 0x02,
    AutoFocus               = 0x04,
    ManualZoom              = 0x05,
    ManualFocus             = 0x06,
    GimbalRotation          = 0x07,
    Center                  = 0x08,
    AcquireConfigInfo       = 0x0A,
    FunctionFeedbackInfo    = 0x0B,
    PhotoAndMode            = 0x0C,
    AcquireGimbalAttitude   = 0x0D,
    AbsoluteZoom            = 0x0F,
    SetCameraImageType      = 0x11,
    GetTempFullImage        = 0x14,
    ReadRangefinder         = 0x15,
    SetThermalPalette       = 0x1B,
    SetThermalRawData       = 0x34,
    SetThermalGain          = 0x38,
};

/// Data byte of CommandId::PhotoAndMode. The one command multiplexes shutter, recording
/// and gimbal motion mode.
enum class PhotoFunction : quint8 {
    TakePicture     = 0,
    ToggleHdr       = 1,
    ToggleRecording = 2,
    LockMode        = 3,
    FollowMode      = 4,
    FpvMode         = 5,
};

enum class MotionMode : quint8 {
    Lock   = 0,
    Follow = 1,
    Fpv    = 2,
};

enum class RecordingStatus : quint8 {
    Off      = 0,
    On       = 1,
    NoCard   = 2,
    DataLoss = 3,
};

/// ZT30 sensor routing. "PIP" combinations render two sensors picture-in-picture on the
/// main stream; the remaining sensor goes to the sub stream.
enum class CameraImageType : quint8 {
    MainPipZoomThermalSubWideAngle = 0,
    MainPipWideAngleThermalSubZoom = 1,
    MainPipZoomWideAngleSubThermal = 2,
    MainZoomSubThermal             = 3,
    MainZoomSubWideAngle           = 4,
    MainWideAngleSubThermal        = 5,
    MainWideAngleSubZoom           = 6,
    MainThermalSubZoom             = 7,
    MainThermalSubWideAngle        = 8,
};

/// Value 1 is absent from the SIYI palette table.
enum class ThermalPalette : quint8 {
    WhiteHot = 0,
    Sepia    = 2,
    IronBow  = 3,
    Rainbow  = 4,
    Night    = 5,
    Aurora   = 6,
    RedHot   = 7,
    Jungle   = 8,
    Medical  = 9,
    BlackHot = 10,
    GloryHot = 11,
};

enum class ThermalGain : quint8 {
    Low  = 0,   ///< 50C ~ 550C
    High = 1,   ///< -20C ~ 150C
};

struct Frame {
    CommandId commandId = CommandId::AcquireFirmwareVersion;
    quint16 sequence = 0;
    bool isAck = false;
    QByteArray data;
};

struct Attitude {
    float yawDeg = 0.0F;
    float pitchDeg = 0.0F;
    float rollDeg = 0.0F;
    float yawRateDegPerSec = 0.0F;
    float pitchRateDegPerSec = 0.0F;
    float rollRateDegPerSec = 0.0F;
};

struct ConfigInfo {
    bool hdrEnabled = false;
    RecordingStatus recordingStatus = RecordingStatus::Off;
    MotionMode motionMode = MotionMode::Lock;
    bool mountedUpsideDown = false;
};

struct FirmwareVersion {
    QString camera;
    QString gimbal;
    QString zoom;   ///< Empty on models that report no zoom firmware.
};

/// Hottest and coldest point of the thermal image, from CommandId::GetTempFullImage.
struct ThermalRange {
    float maxTempC = 0.0F;
    float minTempC = 0.0F;
    quint16 maxX = 0;
    quint16 maxY = 0;
    quint16 minX = 0;
    quint16 minY = 0;
};

[[nodiscard]] quint16 crc16(const QByteArray &data);

/// Wraps `data` in a complete SIYI packet using a raw command byte. SIYI reuses this framing
/// across devices that number their commands differently - the AI tracking module has its own
/// command set - so the byte is not constrained to CommandId.
[[nodiscard]] QByteArray encodeRaw(quint8 commandId, const QByteArray &data = QByteArray(), quint16 sequence = 0);

/// Wraps `data` in a complete SIYI packet. The ack bit is requested so the camera replies
/// even to commands whose response carries no state.
[[nodiscard]] QByteArray encode(CommandId commandId, const QByteArray &data = QByteArray(), quint16 sequence = 0);

/// Rates are scalars in the range -100..100 of the gimbal's maximum slew rate.
[[nodiscard]] QByteArray encodeGimbalRotation(int yawRate, int pitchRate, quint16 sequence = 0);

/// `direction` is 1 to zoom in, -1 to zoom out, 0 to stop.
[[nodiscard]] QByteArray encodeManualZoom(int direction, quint16 sequence = 0);

/// Absolute zoom factor, e.g. 4.5 for 4.5x. Resolution is one decimal place.
[[nodiscard]] QByteArray encodeAbsoluteZoom(float multiple, quint16 sequence = 0);

[[nodiscard]] QByteArray encodeSingleByte(CommandId commandId, quint8 value, quint16 sequence = 0);

/// Requests the full-image minimum/maximum thermal values. The payload value 2 selects
/// the min/max response format used by ZT6 and ZT30 cameras.
[[nodiscard]] QByteArray encodeThermalRangeRequest(quint16 sequence = 0);

/// Pulls every complete, CRC-valid frame out of `buffer` and erases the bytes it consumed.
/// Bytes ahead of a header and frames failing CRC are dropped; a trailing partial frame is
/// left in place for the next call.
[[nodiscard]] QList<Frame> decode(QByteArray &buffer);

[[nodiscard]] std::optional<Attitude> parseAttitude(const QByteArray &data);
[[nodiscard]] std::optional<ConfigInfo> parseConfigInfo(const QByteArray &data);
[[nodiscard]] std::optional<FirmwareVersion> parseFirmwareVersion(const QByteArray &data);
[[nodiscard]] std::optional<ThermalRange> parseThermalRange(const QByteArray &data);

/// Zoom factor reported by the camera in reply to a manual zoom command.
[[nodiscard]] std::optional<float> parseZoomMultiple(const QByteArray &data);

/// Laser rangefinder distance in metres. ZT30 only.
[[nodiscard]] std::optional<float> parseRangefinderDistance(const QByteArray &data);

/// Model name decoded from the first two hardware id characters, e.g. "ZT30".
/// Empty when the id is not one this driver knows.
[[nodiscard]] QString parseHardwareModel(const QByteArray &data);

} // namespace SiyiProtocol
