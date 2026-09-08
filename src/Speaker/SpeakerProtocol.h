#pragma once

#include <optional>

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QtTypes>

/// \brief Command codec for the loudspeaker payload.
///
/// The frame layout is the SIYI camera one - STX(2) CTRL(1) DATA_LEN(2) SEQ(2) CMD(1) DATA CRC16(2),
/// little endian, CRC-16/XMODEM over every preceding byte - so the payload reuses an existing CRC and
/// we lift SiyiProtocol::crc16. Two things differ from the camera, both forced by the SIYI datalink:
/// the STX is 0xA5 0x5A, not 0x55 0x66, because the controller's RC MCU eats 0x55 0x66 frames as its
/// own SDK instead of forwarding them to the air unit; and the command numbering is our own.
namespace SpeakerProtocol {

/// Factory defaults. The payload is reached through the controller's local SIYI UDP<->serial bridge
/// (see SiyiBridgeController), so this is loopback on the controller, not the payload's own IP.
inline constexpr char kDefaultAddress[] = "127.0.0.1";
inline constexpr quint16 kDefaultPort = 19856;

enum class CommandId : quint8 {
    Play        = 0x01,   ///< Data: track number (1 based).
    Stop        = 0x02,
    SetVolume   = 0x03,   ///< Data: 0-100 percent.
    RequestState = 0x04,
};

/// Reply to CommandId::RequestState, and pushed by the payload roughly once a second.
struct State {
    bool playing = false;
    quint8 track = 0;       ///< Track being played, 0 when idle.
    quint8 volume = 0;      ///< Percent.
    quint8 trackCount = 0;  ///< Audio files the payload holds.
};

/// One decoded packet: the command byte, its sequence and the raw data field.
struct Frame {
    quint8 commandId = 0;
    quint16 sequence = 0;
    QByteArray data;
};

[[nodiscard]] QByteArray encodePlay(quint8 track, quint16 sequence = 0);
[[nodiscard]] QByteArray encodeStop(quint16 sequence = 0);
[[nodiscard]] QByteArray encodeSetVolume(quint8 percent, quint16 sequence = 0);
[[nodiscard]] QByteArray encodeRequestState(quint16 sequence = 0);

/// Pulls every complete, CRC-valid frame out of `buffer` and erases the bytes it consumed. Bytes
/// ahead of a header and frames failing CRC are dropped; a trailing partial frame is left in place.
[[nodiscard]] QList<Frame> decode(QByteArray &buffer);

[[nodiscard]] std::optional<State> parseState(const QByteArray &data);

} // namespace SpeakerProtocol
