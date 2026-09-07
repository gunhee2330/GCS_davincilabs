#include "SiyiProtocol.h"

#include <QtCore/QtGlobal>
#include <QtCore/QtNumeric>

namespace {

/// Upper bound on DATA_LEN accepted while framing. Real SIYI replies are a few dozen bytes;
/// this only stops a corrupt length field from stalling the parser forever.
constexpr int kMaxDataSize = 512;

constexpr int kDataOffset = 8;
constexpr quint8 kCtrlNeedAck = 0x01;
constexpr quint8 kCtrlIsAck = 0x02;

quint16 readUint16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<quint8>(data.at(offset)) |
                                (static_cast<quint8>(data.at(offset + 1)) << 8));
}

qint16 readInt16(const QByteArray &data, int offset)
{
    return static_cast<qint16>(readUint16(data, offset));
}

qint32 readInt32(const QByteArray &data, int offset)
{
    return static_cast<qint32>(static_cast<quint32>(readUint16(data, offset)) |
                               (static_cast<quint32>(readUint16(data, offset + 2)) << 16));
}

QString formatVersion(const QByteArray &data, int offset)
{
    // Each version is stored patch, minor, major on ascending bytes.
    return QStringLiteral("%1.%2.%3")
        .arg(static_cast<quint8>(data.at(offset + 2)))
        .arg(static_cast<quint8>(data.at(offset + 1)))
        .arg(static_cast<quint8>(data.at(offset)));
}

} // namespace

namespace SiyiProtocol {

quint16 crc16(const QByteArray &data)
{
    quint16 crc = 0;
    for (const char byte : data) {
        crc ^= static_cast<quint16>(static_cast<quint8>(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<quint16>((crc << 1) ^ 0x1021) : static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}

QByteArray encode(CommandId commandId, const QByteArray &data, quint16 sequence)
{
    return encodeRaw(static_cast<quint8>(commandId), data, sequence);
}

QByteArray encodeRaw(quint8 commandId, const QByteArray &data, quint16 sequence)
{
    QByteArray packet;
    packet.reserve(kMinPacketSize + data.size());

    packet.append(static_cast<char>(kHeaderByte1));
    packet.append(static_cast<char>(kHeaderByte2));
    packet.append(static_cast<char>(kCtrlNeedAck));
    packet.append(static_cast<char>(data.size() & 0xFF));
    packet.append(static_cast<char>((data.size() >> 8) & 0xFF));
    packet.append(static_cast<char>(sequence & 0xFF));
    packet.append(static_cast<char>((sequence >> 8) & 0xFF));
    packet.append(static_cast<char>(commandId));
    packet.append(data);

    const quint16 crc = crc16(packet);
    packet.append(static_cast<char>(crc & 0xFF));
    packet.append(static_cast<char>((crc >> 8) & 0xFF));

    return packet;
}

QByteArray encodeGimbalRotation(int yawRate, int pitchRate, quint16 sequence)
{
    const auto clamp = [](int rate) { return static_cast<qint8>(qBound(-100, rate, 100)); };

    QByteArray data;
    data.append(static_cast<char>(clamp(yawRate)));
    data.append(static_cast<char>(clamp(pitchRate)));
    return encode(CommandId::GimbalRotation, data, sequence);
}

QByteArray encodeManualZoom(int direction, quint16 sequence)
{
    quint8 step = 0;
    if (direction > 0) {
        step = 1;
    } else if (direction < 0) {
        // The SDK encodes "zoom out" as -1 in an unsigned byte.
        step = 0xFF;
    }
    return encodeSingleByte(CommandId::ManualZoom, step, sequence);
}

QByteArray encodeAbsoluteZoom(float multiple, quint16 sequence)
{
    const float bounded = qIsFinite(multiple) ? qBound(1.0F, multiple, 255.9F) : 1.0F;
    const int tenths = qRound(bounded * 10.0F);
    const auto integerPart = static_cast<quint8>(tenths / 10);
    const auto fractionPart = static_cast<quint8>(tenths % 10);

    QByteArray data;
    data.append(static_cast<char>(integerPart));
    data.append(static_cast<char>(fractionPart));
    return encode(CommandId::AbsoluteZoom, data, sequence);
}

QByteArray encodeSingleByte(CommandId commandId, quint8 value, quint16 sequence)
{
    QByteArray data;
    data.append(static_cast<char>(value));
    return encode(commandId, data, sequence);
}

QByteArray encodeThermalRangeRequest(quint16 sequence)
{
    return encodeSingleByte(CommandId::GetTempFullImage, 2, sequence);
}

QList<Frame> decode(QByteArray &buffer)
{
    QList<Frame> frames;
    int consumed = 0;

    while ((buffer.size() - consumed) >= kMinPacketSize) {
        const auto byteAt = [&buffer, consumed](int offset) {
            return static_cast<quint8>(buffer.at(consumed + offset));
        };

        if ((byteAt(0) != kHeaderByte1) || (byteAt(1) != kHeaderByte2)) {
            ++consumed;
            continue;
        }

        const int dataSize = byteAt(3) | (byteAt(4) << 8);
        if (dataSize > kMaxDataSize) {
            ++consumed;
            continue;
        }

        const int packetSize = kMinPacketSize + dataSize;
        if ((buffer.size() - consumed) < packetSize) {
            // Rest of the packet has not arrived yet.
            break;
        }

        const quint16 receivedCrc = static_cast<quint16>(byteAt(packetSize - 2) | (byteAt(packetSize - 1) << 8));
        if (crc16(buffer.mid(consumed, packetSize - 2)) != receivedCrc) {
            ++consumed;
            continue;
        }

        Frame frame;
        frame.isAck = (byteAt(2) & kCtrlIsAck) != 0;
        frame.sequence = static_cast<quint16>(byteAt(5) | (byteAt(6) << 8));
        frame.commandId = static_cast<CommandId>(byteAt(7));
        frame.data = buffer.mid(consumed + kDataOffset, dataSize);
        frames.append(frame);

        consumed += packetSize;
    }

    if (consumed > 0) {
        buffer.remove(0, consumed);
    }

    return frames;
}

std::optional<Attitude> parseAttitude(const QByteArray &data)
{
    if (data.size() < 12) {
        return std::nullopt;
    }

    // Angles and rates are reported in the camera's own frame, in tenths of a degree.
    Attitude attitude;
    attitude.yawDeg = readInt16(data, 0) * 0.1F;
    attitude.pitchDeg = readInt16(data, 2) * 0.1F;
    attitude.rollDeg = readInt16(data, 4) * 0.1F;
    attitude.yawRateDegPerSec = readInt16(data, 6) * 0.1F;
    attitude.pitchRateDegPerSec = readInt16(data, 8) * 0.1F;
    attitude.rollRateDegPerSec = readInt16(data, 10) * 0.1F;
    return attitude;
}

std::optional<ConfigInfo> parseConfigInfo(const QByteArray &data)
{
    if (data.size() < 7) {
        return std::nullopt;
    }

    ConfigInfo info;
    info.hdrEnabled = static_cast<quint8>(data.at(1)) != 0;
    info.recordingStatus = static_cast<RecordingStatus>(static_cast<quint8>(data.at(3)));
    info.motionMode = static_cast<MotionMode>(static_cast<quint8>(data.at(4)));
    info.mountedUpsideDown = static_cast<quint8>(data.at(5)) == 2;
    return info;
}

std::optional<FirmwareVersion> parseFirmwareVersion(const QByteArray &data)
{
    // Models without a separate zoom module report 8 bytes instead of 12.
    if (data.size() < 8) {
        return std::nullopt;
    }

    FirmwareVersion version;
    version.camera = formatVersion(data, 0);
    version.gimbal = formatVersion(data, 4);
    if (data.size() >= 12) {
        version.zoom = formatVersion(data, 8);
    }
    return version;
}

std::optional<ThermalRange> parseThermalRange(const QByteArray &data)
{
    if (data.size() < 12) {
        return std::nullopt;
    }

    ThermalRange range;
    range.maxTempC = readInt16(data, 0) * 0.01F;
    range.minTempC = readInt16(data, 2) * 0.01F;
    range.maxX = readUint16(data, 4);
    range.maxY = readUint16(data, 6);
    range.minX = readUint16(data, 8);
    range.minY = readUint16(data, 10);
    return range;
}

std::optional<float> parseZoomMultiple(const QByteArray &data)
{
    if (data.size() < 2) {
        return std::nullopt;
    }
    return readUint16(data, 0) * 0.1F;
}

std::optional<float> parseRangefinderDistance(const QByteArray &data)
{
    if (data.size() < 2) {
        return std::nullopt;
    }

    // Decimetres, not metres. Measured on the airframe: a wall a few metres away came back as
    // 65, and the pod's own minimum range is 5 m, which is the documented minimum of 50.
    const quint16 decimetres = readUint16(data, 0);

    // The same "no measurement" the target parser refuses: an unlit laser, or a lit one that
    // got no return off sky, glass or water, answers with zeroes. Passing that on prints
    // "LRF 0.0 m" over a shot that never landed instead of blanking the readout. Only zero is
    // refused, not everything under the documented 50 dm minimum - a real sensor reading a
    // little short of its own spec is still a reading.
    if (decimetres == 0) {
        return std::nullopt;
    }
    return static_cast<float>(decimetres) * 0.1F;
}

std::optional<RangefinderTarget> parseRangefinderTarget(const QByteArray &data)
{
    if (data.size() < 8) {
        return std::nullopt;
    }

    // Longitude first, then latitude — the order the manual gives, and the reverse of the one
    // every other geographic pair in this codebase uses.
    RangefinderTarget target;
    target.lonDeg = readInt32(data, 0) * 1e-7;
    target.latDeg = readInt32(data, 4) * 1e-7;

    // Out of range means the pod answered without a fix rather than with a position.
    if ((target.lonDeg < -180.0) || (target.lonDeg > 180.0) ||
        (target.latDeg < -90.0) || (target.latDeg > 90.0)) {
        return std::nullopt;
    }

    // An unlit or unreturned laser answers with eight zero bytes, and zero, zero is a real
    // place in the Gulf of Guinea: taken at face value it puts a target marker on the map for
    // a measurement that never happened.
    if (qFuzzyIsNull(target.lonDeg) && qFuzzyIsNull(target.latDeg)) {
        return std::nullopt;
    }
    return target;
}

QByteArray encodeSetLaserState(bool on, quint16 sequence)
{
    QByteArray data;
    data.append(static_cast<char>(on ? 1 : 0));
    return encodeRaw(static_cast<quint8>(CommandId::SetLaserState), data, sequence);
}

std::optional<bool> parseLaserState(const QByteArray &data)
{
    if (data.isEmpty()) {
        return std::nullopt;
    }
    return static_cast<quint8>(data.at(0)) != 0;
}

QString parseHardwareModel(const QByteArray &data)
{
    if (data.size() < 2) {
        return QString();
    }

    // The first two characters of the hardware id identify the product.
    struct ModelId {
        char id[2];
        const char *name;
    };
    static constexpr ModelId kModels[] = {
        {{'7', '5'}, "A2"},
        {{'7', '3'}, "A8"},
        {{'6', 'B'}, "ZR10"},
        {{'7', '8'}, "ZR30"},
        {{'8', '3'}, "ZT6"},
        {{'7', 'A'}, "ZT30"},
    };

    for (const ModelId &model : kModels) {
        if ((data.at(0) == model.id[0]) && (data.at(1) == model.id[1])) {
            return QString::fromLatin1(model.name);
        }
    }

    return QString();
}

} // namespace SiyiProtocol
