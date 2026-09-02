#pragma once

#include <optional>

#include <QtCore/QByteArray>
#include <QtCore/QtTypes>

/// \brief Command codec for the loudspeaker payload.
///
/// The payload reuses the SIYI packet framing already implemented for the camera, so the
/// aircraft carries one wire format rather than two and the payload side can lift an
/// existing CRC implementation. Only the command numbering is our own.
namespace SpeakerProtocol {

/// Factory defaults for the payload on the aircraft network.
inline constexpr char kDefaultAddress[] = "192.168.144.70";
inline constexpr quint16 kDefaultPort = 37270;

enum class CommandId : quint8 {
    Play        = 0x01,   ///< Data: track number (1 based).
    Stop        = 0x02,
    SetVolume   = 0x03,   ///< Data: 0-100 percent.
    RequestState = 0x04,
};

/// Reply to CommandId::RequestState, and pushed by the payload once a second.
struct State {
    bool playing = false;
    quint8 track = 0;       ///< Track being played, 0 when idle.
    quint8 volume = 0;      ///< Percent.
    quint8 trackCount = 0;  ///< Audio files the payload holds.
};

[[nodiscard]] QByteArray encodePlay(quint8 track, quint16 sequence = 0);
[[nodiscard]] QByteArray encodeStop(quint16 sequence = 0);
[[nodiscard]] QByteArray encodeSetVolume(quint8 percent, quint16 sequence = 0);
[[nodiscard]] QByteArray encodeRequestState(quint16 sequence = 0);

[[nodiscard]] std::optional<State> parseState(const QByteArray &data);

} // namespace SpeakerProtocol
