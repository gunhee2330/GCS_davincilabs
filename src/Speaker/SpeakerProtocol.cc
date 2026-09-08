#include "SpeakerProtocol.h"

#include <QtCore/QtGlobal>

#include "SiyiProtocol.h"

namespace {

// Loudspeaker framing: SIYI's byte layout with a different header. See SpeakerProtocol.h for why.
constexpr quint8 kHeaderByte1 = 0xA5;
constexpr quint8 kHeaderByte2 = 0x5A;
constexpr quint8 kCtrl = 0x02;
constexpr int kHeaderLen = 8;       // STX(2) CTRL(1) DATA_LEN(2) SEQ(2) CMD(1)
constexpr int kCrcLen = 2;
constexpr int kMinPacketSize = kHeaderLen + kCrcLen;
constexpr int kMaxDataLen = 512;    // guards against a false header inflating the frame length

QByteArray encodeFrame(SpeakerProtocol::CommandId commandId, const QByteArray &data, quint16 sequence)
{
    QByteArray packet;
    packet.reserve(kMinPacketSize + data.size());

    packet.append(static_cast<char>(kHeaderByte1));
    packet.append(static_cast<char>(kHeaderByte2));
    packet.append(static_cast<char>(kCtrl));
    packet.append(static_cast<char>(data.size() & 0xFF));
    packet.append(static_cast<char>((data.size() >> 8) & 0xFF));
    packet.append(static_cast<char>(sequence & 0xFF));
    packet.append(static_cast<char>((sequence >> 8) & 0xFF));
    packet.append(static_cast<char>(static_cast<quint8>(commandId)));
    packet.append(data);

    const quint16 crc = SiyiProtocol::crc16(packet);
    packet.append(static_cast<char>(crc & 0xFF));
    packet.append(static_cast<char>((crc >> 8) & 0xFF));

    return packet;
}

QByteArray encodeSingleByte(SpeakerProtocol::CommandId commandId, quint8 value, quint16 sequence)
{
    QByteArray data;
    data.append(static_cast<char>(value));
    return encodeFrame(commandId, data, sequence);
}

} // namespace

namespace SpeakerProtocol {

QByteArray encodePlay(quint8 track, quint16 sequence)
{
    return encodeSingleByte(CommandId::Play, track, sequence);
}

QByteArray encodeStop(quint16 sequence)
{
    return encodeFrame(CommandId::Stop, QByteArray(), sequence);
}

QByteArray encodeSetVolume(quint8 percent, quint16 sequence)
{
    return encodeSingleByte(CommandId::SetVolume, qBound<quint8>(0, percent, 100), sequence);
}

QByteArray encodeRequestState(quint16 sequence)
{
    return encodeFrame(CommandId::RequestState, QByteArray(), sequence);
}

QList<Frame> decode(QByteArray &buffer)
{
    QList<Frame> frames;
    int consumed = 0;

    while (buffer.size() - consumed >= kMinPacketSize) {
        if ((static_cast<quint8>(buffer.at(consumed)) != kHeaderByte1) ||
            (static_cast<quint8>(buffer.at(consumed + 1)) != kHeaderByte2)) {
            ++consumed;
            continue;
        }

        const int dataLen = static_cast<quint8>(buffer.at(consumed + 3)) |
                            (static_cast<quint8>(buffer.at(consumed + 4)) << 8);
        if (dataLen > kMaxDataLen) {
            ++consumed;
            continue;
        }

        const int total = kHeaderLen + dataLen + kCrcLen;
        if (buffer.size() - consumed < total) {
            break;  // rest has not arrived yet
        }

        const QByteArray frame = buffer.mid(consumed, total);
        const quint16 receivedCrc = static_cast<quint8>(frame.at(total - kCrcLen)) |
                                   (static_cast<quint8>(frame.at(total - kCrcLen + 1)) << 8);
        if (SiyiProtocol::crc16(frame.left(total - kCrcLen)) != receivedCrc) {
            ++consumed;
            continue;
        }

        Frame decoded;
        decoded.sequence = static_cast<quint8>(frame.at(5)) | (static_cast<quint8>(frame.at(6)) << 8);
        decoded.commandId = static_cast<quint8>(frame.at(7));
        decoded.data = frame.mid(kHeaderLen, dataLen);
        frames.append(decoded);
        consumed += total;
    }

    buffer.remove(0, consumed);
    return frames;
}

std::optional<State> parseState(const QByteArray &data)
{
    if (data.size() < 4) {
        return std::nullopt;
    }

    State state;
    state.playing = static_cast<quint8>(data.at(0)) != 0;
    state.track = static_cast<quint8>(data.at(1));
    state.volume = static_cast<quint8>(data.at(2));
    state.trackCount = static_cast<quint8>(data.at(3));
    return state;
}

} // namespace SpeakerProtocol
