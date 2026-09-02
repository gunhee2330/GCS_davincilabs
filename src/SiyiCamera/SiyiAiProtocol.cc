#include "SiyiAiProtocol.h"

#include <QtCore/QCoreApplication>

#include "SiyiProtocol.h"

namespace {

constexpr quint8 kActionCancel = 0;
constexpr quint8 kActionTrack = 1;

quint16 readUint16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<quint8>(data.at(offset)) |
                                (static_cast<quint8>(data.at(offset + 1)) << 8));
}

void appendUint16(QByteArray &data, quint16 value)
{
    data.append(static_cast<char>(value & 0xFF));
    data.append(static_cast<char>((value >> 8) & 0xFF));
}

QByteArray encodeSingleByte(SiyiAi::CommandId commandId, quint8 value, quint16 sequence)
{
    QByteArray data;
    data.append(static_cast<char>(value));
    return SiyiProtocol::encodeRaw(static_cast<quint8>(commandId), data, sequence);
}

/// track_action followed by two corner coordinates; a point pick sends (0,0) as the second corner.
QByteArray encodeTrackAction(quint8 action, quint16 lx, quint16 ly, quint16 rx, quint16 ry, quint16 sequence)
{
    QByteArray data;
    data.append(static_cast<char>(action));
    appendUint16(data, lx);
    appendUint16(data, ly);
    appendUint16(data, rx);
    appendUint16(data, ry);
    return SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiAi::CommandId::SetTrackTarget), data, sequence);
}

} // namespace

namespace SiyiAi {

QByteArray encodeRequest(CommandId commandId, quint16 sequence)
{
    return SiyiProtocol::encodeRaw(static_cast<quint8>(commandId), QByteArray(), sequence);
}

QByteArray encodeSetRecognition(bool enabled, quint16 sequence)
{
    return encodeSingleByte(CommandId::SetRecognitionState, enabled ? 1 : 0, sequence);
}

QByteArray encodeSetTargetStream(bool enabled, quint16 sequence)
{
    return encodeSingleByte(CommandId::SetTargetStreamState, enabled ? 1 : 0, sequence);
}

QByteArray encodeTrackPoint(quint16 x, quint16 y, quint16 sequence)
{
    return encodeTrackAction(kActionTrack, x, y, 0, 0, sequence);
}

QByteArray encodeTrackBox(quint16 left, quint16 top, quint16 right, quint16 bottom, quint16 sequence)
{
    return encodeTrackAction(kActionTrack, left, top, right, bottom, sequence);
}

QByteArray encodeCancelTracking(quint16 sequence)
{
    return encodeTrackAction(kActionCancel, 0, 0, 0, 0, sequence);
}

std::optional<TrackedTarget> parseTargetStream(const QByteArray &data)
{
    if (data.size() < 10) {
        return std::nullopt;
    }

    TrackedTarget target;
    target.centreX = readUint16(data, 0);
    target.centreY = readUint16(data, 2);
    target.width = readUint16(data, 4);
    target.height = readUint16(data, 6);
    target.type = static_cast<TargetType>(static_cast<quint8>(data.at(8)));
    target.status = static_cast<TrackingStatus>(static_cast<quint8>(data.at(9)));
    return target;
}

std::optional<bool> parseEnabledFlag(const QByteArray &data)
{
    if (data.isEmpty()) {
        return std::nullopt;
    }
    return static_cast<quint8>(data.at(0)) != 0;
}

std::optional<TrackRequestResult> parseTrackRequestResult(const QByteArray &data)
{
    if (data.isEmpty()) {
        return std::nullopt;
    }
    return static_cast<TrackRequestResult>(static_cast<quint8>(data.at(0)));
}

std::optional<TargetStreamState> parseTargetStreamState(const QByteArray &data)
{
    if (data.isEmpty()) {
        return std::nullopt;
    }
    return static_cast<TargetStreamState>(static_cast<quint8>(data.at(0)));
}

QString targetTypeName(TargetType type)
{
    switch (type) {
    case TargetType::Person:
        return QCoreApplication::translate("SiyiAi", "Person");
    case TargetType::Car:
        return QCoreApplication::translate("SiyiAi", "Car");
    case TargetType::Bus:
        return QCoreApplication::translate("SiyiAi", "Bus");
    case TargetType::Truck:
        return QCoreApplication::translate("SiyiAi", "Truck");
    case TargetType::Arbitrary:
        return QCoreApplication::translate("SiyiAi", "Object");
    }
    return QCoreApplication::translate("SiyiAi", "Unknown");
}

} // namespace SiyiAi
