#include "SiyiLongProtocol.h"

namespace {

constexpr quint8 kHeaderBytes[] = {0x55, 0x66, 0xAA, 0xBB};

constexpr int kHeaderSize = 12;     ///< Bytes covered by the header CRC32.
constexpr int kDataOffset = 16;     ///< Header plus its CRC32.
constexpr int kMinFrameSize = 20;   ///< Both CRC32s included, i.e. a frame with no payload.

constexpr quint8 kCtrlNeedAck = 0x01;
constexpr quint8 kCtrlIsAck = 0x02;

/// Upper bound on DATA_LEN accepted while framing. The largest real reply is the 0xD5 class
/// list - one mask byte per class plus a comma separated name string - which is well under a
/// kilobyte. The bound exists so a corrupt length can never size a buffer or stall the parser:
/// this link carries no version information, so a firmware update that repurposes the field
/// would otherwise hand us a multi-gigabyte allocation.
constexpr int kMaxDataSize = 4096;

quint32 readUint32(const QByteArray &data, int offset)
{
    quint32 value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<quint32>(static_cast<quint8>(data.at(offset + index))) << (index * 8);
    }
    return value;
}

void appendUint32(QByteArray &data, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        data.append(static_cast<char>((value >> shift) & 0xFF));
    }
}

} // namespace

namespace SiyiLongProtocol {

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0;
    for (const char byte : data) {
        crc ^= static_cast<quint32>(static_cast<quint8>(byte)) << 24;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80000000U) ? ((crc << 1) ^ 0x04C11DB7U) : (crc << 1);
        }
    }
    return crc;
}

QByteArray encode(quint8 commandId, const QByteArray &data, quint16 sequence)
{
    QByteArray frame;
    frame.reserve(kMinFrameSize + data.size());

    for (const quint8 byte : kHeaderBytes) {
        frame.append(static_cast<char>(byte));
    }
    frame.append(static_cast<char>(kCtrlNeedAck));
    appendUint32(frame, static_cast<quint32>(data.size()));
    frame.append(static_cast<char>(sequence & 0xFF));
    frame.append(static_cast<char>((sequence >> 8) & 0xFF));
    frame.append(static_cast<char>(commandId));

    const quint32 headerCrc = crc32(frame);
    appendUint32(frame, headerCrc);

    frame.append(data);

    const quint32 frameCrc = crc32(frame);
    appendUint32(frame, frameCrc);

    return frame;
}

QList<Frame> decode(QByteArray &buffer)
{
    QList<Frame> frames;
    int consumed = 0;

    while ((buffer.size() - consumed) >= kMinFrameSize) {
        const auto byteAt = [&buffer, consumed](int offset) {
            return static_cast<quint8>(buffer.at(consumed + offset));
        };

        if ((byteAt(0) != kHeaderBytes[0]) || (byteAt(1) != kHeaderBytes[1]) ||
            (byteAt(2) != kHeaderBytes[2]) || (byteAt(3) != kHeaderBytes[3])) {
            ++consumed;
            continue;
        }

        // The header CRC is checked before DATA_LEN is used for anything. Four magic bytes turn
        // up in stream garbage often enough that the length behind them cannot be trusted until
        // the header as a whole has been vouched for.
        if (readUint32(buffer, consumed + kHeaderSize) != crc32(buffer.mid(consumed, kHeaderSize))) {
            ++consumed;
            continue;
        }

        const quint32 dataSize = readUint32(buffer, consumed + 5);
        if (dataSize > static_cast<quint32>(kMaxDataSize)) {
            ++consumed;
            continue;
        }

        const int frameSize = kMinFrameSize + static_cast<int>(dataSize);
        if ((buffer.size() - consumed) < frameSize) {
            // Rest of the frame has not arrived yet.
            break;
        }

        if (readUint32(buffer, consumed + frameSize - 4) != crc32(buffer.mid(consumed, frameSize - 4))) {
            ++consumed;
            continue;
        }

        Frame frame;
        frame.isAck = (byteAt(4) & kCtrlIsAck) != 0;
        frame.sequence = static_cast<quint16>(byteAt(9) | (byteAt(10) << 8));
        frame.commandId = byteAt(11);
        frame.data = buffer.mid(consumed + kDataOffset, static_cast<int>(dataSize));
        frames.append(frame);

        consumed += frameSize;
    }

    if (consumed > 0) {
        buffer.remove(0, consumed);
    }

    return frames;
}

} // namespace SiyiLongProtocol
