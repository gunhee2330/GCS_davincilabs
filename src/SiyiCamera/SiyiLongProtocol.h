#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QtTypes>

/// \brief Codec for SIYI's "long" frame, the framing of the private camera SDK the AI tracking
/// module speaks on TCP 37256.
///
///     STX(4) | CTRL(1) | DATA_LEN(4) | SEQ(2) | CMD_ID(1) | CRC32(4) | DATA | CRC32(4)
///
/// STX is 0x55 0x66 0xAA 0xBB, every multi-byte field including both checksums is little endian,
/// and a whole frame is DATA_LEN + 20 bytes. The first CRC32 covers the twelve header bytes, the
/// second covers every byte ahead of it.
///
/// None of this is in any SIYI manual - the documented SDK is the CRC16 framing in SiyiProtocol,
/// on a different port. This layout was read out of the module firmware
/// (long_protocol_tcp_server_send@0x560b10) and the UniGCS app
/// (biz/siyi/protocol/bu/camera/siyi/o.java:683-724, which agree byte for byte). Those two are
/// the specification; there is nothing else to check a guess against.
///
/// It gets its own codec rather than a mode flag on SiyiProtocol because nothing is shared:
/// different magic, a four byte length instead of two, two checksums instead of one, and a
/// different CRC.
namespace SiyiLongProtocol {

/// A decoded frame. CMD_ID stays a raw byte because this link has its own command set - the
/// module's 37256 dispatch table - which overlaps the documented SiyiProtocol::CommandId numbers
/// while meaning something else entirely.
struct Frame {
    quint8 commandId = 0;
    quint16 sequence = 0;
    bool isAck = false;     ///< CTRL bit1. Set on the module's replies and on its unsolicited pushes.
    QByteArray data;
};

/// CRC-32 with polynomial 0x04C11DB7, initial value 0, **not** reflected and with no final xor.
///
/// This is not the CRC32 in zlib or qChecksum: those are the reflected variant and produce
/// different values for the same bytes. Ported from the app's implementation
/// (biz/siyi/protocol/bu/camera/siyi/z.java:141) and confirmed against the module firmware, whose
/// table at 0xc5fe90 has 0x04C11DB7 in entry [1] as a non-reflected MSB-first table must.
[[nodiscard]] quint32 crc32(const QByteArray &data);

/// Wraps `data` in a complete frame, with CTRL asking the module to acknowledge.
[[nodiscard]] QByteArray encode(quint8 commandId, const QByteArray &data = QByteArray(), quint16 sequence = 0);

/// Pulls every complete, CRC-valid frame out of `buffer` and erases the bytes it consumed.
///
/// Bytes ahead of a header, frames failing either CRC, and headers claiming an implausible
/// DATA_LEN are dropped by resynchronising on the next byte; a trailing partial frame is left in
/// place for the next call. On return the buffer holds less than one maximum frame, so a caller
/// that decodes after every read cannot be made to grow it without bound.
///
/// Resynchronisation is not a theoretical nicety here: the module's transmit ring is never
/// cleared when a client goes away, so the first bytes after a reconnect can be the truncated
/// tail of a frame addressed to whoever was connected before.
[[nodiscard]] QList<Frame> decode(QByteArray &buffer);

} // namespace SiyiLongProtocol
