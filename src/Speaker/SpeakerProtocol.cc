#include "SpeakerProtocol.h"

#include <QtCore/QtGlobal>

#include "SiyiProtocol.h"

namespace {

QByteArray encodeSingleByte(SpeakerProtocol::CommandId commandId, quint8 value, quint16 sequence)
{
    QByteArray data;
    data.append(static_cast<char>(value));
    return SiyiProtocol::encodeRaw(static_cast<quint8>(commandId), data, sequence);
}

} // namespace

namespace SpeakerProtocol {

QByteArray encodePlay(quint8 track, quint16 sequence)
{
    return encodeSingleByte(CommandId::Play, track, sequence);
}

QByteArray encodeStop(quint16 sequence)
{
    return SiyiProtocol::encodeRaw(static_cast<quint8>(CommandId::Stop), QByteArray(), sequence);
}

QByteArray encodeSetVolume(quint8 percent, quint16 sequence)
{
    return encodeSingleByte(CommandId::SetVolume, qBound<quint8>(0, percent, 100), sequence);
}

QByteArray encodeRequestState(quint16 sequence)
{
    return SiyiProtocol::encodeRaw(static_cast<quint8>(CommandId::RequestState), QByteArray(), sequence);
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
