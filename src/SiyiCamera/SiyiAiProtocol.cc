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

QByteArray encodeSetVideoStream(bool enabled, quint16 sequence)
{
    return encodeSingleByte(CommandId::SetVideoStream, enabled ? 1 : 0, sequence);
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
    // Nine bytes, the full command table form, even though the bench answers sta 0. The module
    // reads touch_rx/touch_ry at payload offsets 5 and 7 unconditionally, whatever DATA_LEN
    // says, and its receive buffer is not cleared between frames - a short cancel makes those
    // two fields the previous frame's residue, which can arm a fresh selection box instead of
    // clearing one. The bench refusal is not a length problem: when the module's own selection
    // flag is already down, no 0x06 payload cancels anything.
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

std::optional<ObjectCountReport> parseObjectCountReport(const QByteArray &data)
{
    // {mode, model, class_count, tally[class_count]}. The "counting is off" reply stops after the
    // model byte, and the reply to a bare state query stops after the class count.
    if (data.isEmpty()) {
        return std::nullopt;
    }

    ObjectCountReport report;
    report.counting = static_cast<quint8>(data.at(0)) == static_cast<quint8>(ObjectCountMode::Start);
    if (!report.counting || (data.size() < 3)) {
        return report;
    }

    const int classes = static_cast<quint8>(data.at(2));
    if (data.size() < (3 + classes)) {
        // Either the state reply, which names no tallies, or a push cut short. Both leave the
        // caller's last good counts to go stale on their own rather than replacing them with a
        // partial row.
        return report;
    }

    report.counts.reserve(classes);
    for (int index = 0; index < classes; ++index) {
        report.counts.append(static_cast<quint8>(data.at(3 + index)));
    }
    return report;
}

QStringList parseObjectClassNames(const QByteArray &data)
{
    // {0x03, model, class_count, mask[class_count], "name,name,name\0"}.
    if ((data.size() < 3) || (static_cast<quint8>(data.at(0)) != static_cast<quint8>(ObjectCountMode::ClassList))) {
        return {};
    }

    const int classes = static_cast<quint8>(data.at(2));
    if (data.size() <= (3 + classes)) {
        return {};
    }

    QByteArray names = data.mid(3 + classes);
    const int terminator = names.indexOf('\0');
    if (terminator >= 0) {
        names.truncate(terminator);
    }

    // Empty parts are kept: the tallies that follow are matched to this list by position, so a
    // class the module names as nothing still has to occupy its slot.
    QStringList list;
    for (const QString &name : QString::fromLatin1(names).split(QLatin1Char(','))) {
        list.append(name.trimmed());
    }
    // One trailing empty part, and only when dropping it is what makes the count match: a blob
    // ending in a separator ("person,car,bus,") yields a part that is punctuation, while the same
    // blob for a module that leaves its last class unnamed ("person,car,") yields one that is a
    // slot. Stripping every trailing empty threw the second case away and returned nothing, which
    // the poll then re-read to the same answer for the rest of the flight.
    if ((list.size() == (classes + 1)) && list.constLast().isEmpty()) {
        list.removeLast();
    }

    // A list that does not name every class cannot be indexed safely, and guessing which names
    // went missing is how people end up counted as cars.
    return (list.size() == classes) ? list : QStringList();
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
