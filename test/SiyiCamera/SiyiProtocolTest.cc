#include "SiyiProtocolTest.h"

#include "SiyiAiProtocol.h"
#include "SiyiLongProtocol.h"
#include "SiyiProtocol.h"
#include "SpeakerProtocol.h"

using namespace SiyiProtocol;

namespace {

QByteArray fromHex(const char *hex)
{
    return QByteArray::fromHex(QByteArray(hex));
}

/// Builds a well-formed packet without going through encode(), so decode() is tested
/// against an independent construction.
QByteArray makePacket(quint8 commandId, const QByteArray &data, quint16 sequence = 0, quint8 ctrl = 1)
{
    QByteArray packet;
    packet.append(static_cast<char>(0x55));
    packet.append(static_cast<char>(0x66));
    packet.append(static_cast<char>(ctrl));
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

QByteArray int16le(qint16 value)
{
    QByteArray out;
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    return out;
}

} // namespace

void SiyiProtocolTest::_encodeMatchesDocumentedFrames_test()
{
    // Byte-for-byte frames quoted in the SIYI gimbal SDK documentation. These pin both the
    // header layout and the CRC variant (CRC-16/XMODEM, little endian on the wire).
    QCOMPARE(encode(CommandId::AcquireFirmwareVersion), fromHex("5566010000000001" "64c4"));
    QCOMPARE(encode(CommandId::AcquireConfigInfo), fromHex("556601000000000a" "0f75"));
    QCOMPARE(encode(CommandId::AcquireGimbalAttitude), fromHex("556601000000000d" "e805"));
    QCOMPARE(encodeSingleByte(CommandId::AutoFocus, 1), fromHex("55660101000000" "0401" "bc57"));
    QCOMPARE(encodeSingleByte(CommandId::Center, 1), fromHex("55660101000000" "0801" "d112"));
    QCOMPARE(encodeSingleByte(CommandId::PhotoAndMode, static_cast<quint8>(PhotoFunction::TakePicture)),
             fromHex("55660101000000" "0c00" "34ce"));
    QCOMPARE(encodeManualZoom(1), fromHex("55660101000000" "0501" "8d64"));
}

void SiyiProtocolTest::_encodeGimbalRotationClampsRates_test()
{
    // Yaw precedes pitch in the payload.
    const QByteArray packet = encodeGimbalRotation(100, -100);
    QCOMPARE(packet.size(), 12);
    QCOMPARE(static_cast<quint8>(packet.at(8)), static_cast<quint8>(100));
    QCOMPARE(static_cast<quint8>(packet.at(9)), static_cast<quint8>(0x9C));    // -100

    const QByteArray clamped = encodeGimbalRotation(5000, -5000);
    QCOMPARE(static_cast<quint8>(clamped.at(8)), static_cast<quint8>(100));
    QCOMPARE(static_cast<quint8>(clamped.at(9)), static_cast<quint8>(0x9C));
}

void SiyiProtocolTest::_encodeAbsoluteZoomSplitsMultiple_test()
{
    const QByteArray packet = encodeAbsoluteZoom(4.5F);
    QCOMPARE(static_cast<quint8>(packet.at(8)), static_cast<quint8>(4));
    QCOMPARE(static_cast<quint8>(packet.at(9)), static_cast<quint8>(5));

    const QByteArray whole = encodeAbsoluteZoom(30.0F);
    QCOMPARE(static_cast<quint8>(whole.at(8)), static_cast<quint8>(30));
    QCOMPARE(static_cast<quint8>(whole.at(9)), static_cast<quint8>(0));

    const QByteArray rounded = encodeAbsoluteZoom(4.99F);
    QCOMPARE(static_cast<quint8>(rounded.at(8)), static_cast<quint8>(5));
    QCOMPARE(static_cast<quint8>(rounded.at(9)), static_cast<quint8>(0));
}

void SiyiProtocolTest::_encodeThermalRangeRequest_test()
{
    QCOMPARE(encodeThermalRangeRequest(), fromHex("55660101000000" "1402" "ac64"));
}

void SiyiProtocolTest::_encodeManualZoomEncodesNegativeAsUnsigned_test()
{
    QCOMPARE(static_cast<quint8>(encodeManualZoom(-1).at(8)), static_cast<quint8>(0xFF));
    QCOMPARE(static_cast<quint8>(encodeManualZoom(0).at(8)), static_cast<quint8>(0));
}

void SiyiProtocolTest::_decodeExtractsFrame_test()
{
    QByteArray buffer = makePacket(static_cast<quint8>(CommandId::AcquireGimbalAttitude), QByteArray(12, '\0'), 7, 3);

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).commandId, CommandId::AcquireGimbalAttitude);
    QCOMPARE(frames.at(0).sequence, static_cast<quint16>(7));
    QVERIFY(frames.at(0).isAck);
    QCOMPARE(frames.at(0).data.size(), 12);
    QVERIFY(buffer.isEmpty());
}

void SiyiProtocolTest::_decodeSkipsLeadingGarbage_test()
{
    QByteArray buffer = fromHex("aabbccdd");
    buffer.append(makePacket(static_cast<quint8>(CommandId::Center), QByteArray(1, '\1')));

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).commandId, CommandId::Center);
    QVERIFY(buffer.isEmpty());
}

void SiyiProtocolTest::_decodeRejectsBadCrc_test()
{
    QByteArray buffer = makePacket(static_cast<quint8>(CommandId::Center), QByteArray(1, '\1'));
    buffer[buffer.size() - 1] = static_cast<char>(buffer.at(buffer.size() - 1) ^ 0xFF);

    const QList<Frame> frames = decode(buffer);
    QVERIFY(frames.isEmpty());
}

void SiyiProtocolTest::_decodeKeepsPartialFrameBuffered_test()
{
    const QByteArray packet = makePacket(static_cast<quint8>(CommandId::AcquireGimbalAttitude), QByteArray(12, '\0'));

    QByteArray buffer = packet.left(packet.size() - 4);
    QVERIFY(decode(buffer).isEmpty());
    QCOMPARE(buffer.size(), packet.size() - 4);

    buffer.append(packet.right(4));
    QCOMPARE(decode(buffer).size(), 1);
    QVERIFY(buffer.isEmpty());
}

void SiyiProtocolTest::_decodeHandlesCoalescedFrames_test()
{
    QByteArray buffer = makePacket(static_cast<quint8>(CommandId::Center), QByteArray(1, '\1'));
    buffer.append(makePacket(static_cast<quint8>(CommandId::AutoFocus), QByteArray(1, '\1')));

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).commandId, CommandId::Center);
    QCOMPARE(frames.at(1).commandId, CommandId::AutoFocus);
}

void SiyiProtocolTest::_parseAttitude_test()
{
    QByteArray data;
    data.append(int16le(1234));      // yaw    123.4 deg
    data.append(int16le(-450));      // pitch  -45.0 deg
    data.append(int16le(10));        // roll     1.0 deg
    data.append(int16le(100));       // yaw rate
    data.append(int16le(-200));      // pitch rate
    data.append(int16le(0));         // roll rate

    const auto attitude = parseAttitude(data);
    QVERIFY(attitude.has_value());
    QCOMPARE(attitude->yawDeg, 123.4F);
    QCOMPARE(attitude->pitchDeg, -45.0F);
    QCOMPARE(attitude->rollDeg, 1.0F);
    QCOMPARE(attitude->yawRateDegPerSec, 10.0F);
    QCOMPARE(attitude->pitchRateDegPerSec, -20.0F);
}

void SiyiProtocolTest::_parseConfigInfo_test()
{
    QByteArray data(7, '\0');
    data[1] = 1;    // HDR on
    data[3] = static_cast<char>(RecordingStatus::On);
    data[4] = static_cast<char>(MotionMode::Follow);
    data[5] = 2;    // mounted upside down

    const auto config = parseConfigInfo(data);
    QVERIFY(config.has_value());
    QVERIFY(config->hdrEnabled);
    QCOMPARE(config->recordingStatus, RecordingStatus::On);
    QCOMPARE(config->motionMode, MotionMode::Follow);
    QVERIFY(config->mountedUpsideDown);
}

void SiyiProtocolTest::_parseHardwareModel_test()
{
    QCOMPARE(parseHardwareModel(QByteArray("7A")), QStringLiteral("ZT30"));
    QCOMPARE(parseHardwareModel(QByteArray("83")), QStringLiteral("ZT6"));
    QCOMPARE(parseHardwareModel(QByteArray("6B")), QStringLiteral("ZR10"));
    QVERIFY(parseHardwareModel(QByteArray("ZZ")).isEmpty());
}

void SiyiProtocolTest::_parseRangefinderTarget_test()
{
    // Longitude first, then latitude, both int32 degE7 little endian: 127.0246810 E, 37.5123456 N.
    // Getting the order the other way round would put this point in the Southern Ocean, so the
    // two values are deliberately different enough for a swap to be obvious.
    const auto target = parseRangefinderTarget(fromHex("9a6db64b" "00ee5b16"));
    QVERIFY(target.has_value());
    QVERIFY(qAbs(target->lonDeg - 127.0246810) < 1e-7);
    QVERIFY(qAbs(target->latDeg - 37.5123456) < 1e-7);

    // A pod that answers without a fix sends values outside the coordinate ranges; those are not
    // a position, and letting them through would put a marker on the map at a real place.
    QVERIFY(!parseRangefinderTarget(fromHex("00000080" "00000080")).has_value());
    QVERIFY(!parseRangefinderTarget(QByteArray(7, '\0')).has_value());

    // An unlit laser answers with eight zero bytes. Zero, zero is inside both ranges and is a
    // real place at sea, so it has to be refused by value rather than by range - observed on the
    // airframe with the laser off.
    QVERIFY(!parseRangefinderTarget(QByteArray(8, '\0')).has_value());
}

void SiyiProtocolTest::_parseRangefinderDistance_test()
{
    // Decimetres on the wire, metres out. 0x0041 = 65 dm is what a wall a few metres away
    // measured on the airframe; reading it as metres put 65 m on the operator's screen.
    const auto range = parseRangefinderDistance(fromHex("4100"));
    QVERIFY(range.has_value());
    QVERIFY(qAbs(*range - 6.5F) < 0.001F);
    QCOMPARE(*parseRangefinderDistance(fromHex("3200")), 5.0F);   // the documented minimum

    // Zero is the pod's "no measurement": an unlit laser, or a lit one that got no return.
    // Accepted it becomes a finite 0.0 m and the window prints "LRF 0.0 m" as a reading.
    QVERIFY(!parseRangefinderDistance(fromHex("0000")).has_value());
}

void SiyiProtocolTest::_laserStateCodec_test()
{
    QCOMPARE(encodeSetLaserState(true, 0), fromHex("55660101000000" "3201" "8ff8"));
    QCOMPARE(encodeSetLaserState(false, 0), fromHex("55660101000000" "3200" "aee8"));

    QCOMPARE(parseLaserState(QByteArray(1, '\1')), std::optional<bool>(true));
    QCOMPARE(parseLaserState(QByteArray(1, '\0')), std::optional<bool>(false));
    QVERIFY(!parseLaserState(QByteArray()).has_value());
}

/// 0xC3 is in no SIYI manual. These bytes are built from the documented framing and the command
/// number UniGCS 3.1.6 sends (biz/siyi/protocol/bu/camera/siyi/{o,h}.java O0(boolean)), computed
/// away from the encoder so a change to either side shows up here.
void SiyiProtocolTest::_aiFollowCodec_test()
{
    QCOMPARE(encodeSingleByte(CommandId::AiFollow, 1, 0), fromHex("55660101000000" "c301" "7fd8"));
    QCOMPARE(encodeSingleByte(CommandId::AiFollow, 0, 0), fromHex("55660101000000" "c300" "5ec8"));
}

void SiyiProtocolTest::_parseRejectsShortPayloads_test()
{
    QVERIFY(!parseRangefinderTarget(QByteArray(7, '\0')).has_value());
    QVERIFY(!parseAttitude(QByteArray(11, '\0')).has_value());
    QVERIFY(!parseConfigInfo(QByteArray(6, '\0')).has_value());
    QVERIFY(!parseFirmwareVersion(QByteArray(7, '\0')).has_value());
    QVERIFY(!parseThermalRange(QByteArray(11, '\0')).has_value());
    QVERIFY(!parseZoomMultiple(QByteArray(1, '\0')).has_value());
    QVERIFY(!parseRangefinderDistance(QByteArray(1, '\0')).has_value());
}

void SiyiProtocolTest::_aiEncodeMatchesFraming_test()
{
    // The AI module reuses the camera framing with its own command numbering. Enabling
    // recognition (0x04, payload 1) is byte-identical to the camera's documented auto focus
    // frame, which pins the shared CRC across both codecs.
    QCOMPARE(SiyiAi::encodeSetRecognition(true), fromHex("55660101000000" "0401" "bc57"));
    QCOMPARE(SiyiAi::encodeSetTargetStream(true), fromHex("55660101000000" "0901" "e021"));
    QCOMPARE(SiyiAi::encodeRequest(SiyiAi::CommandId::RequestRecognitionState),
             fromHex("5566010000000003" "26e4"));
}

void SiyiProtocolTest::_aiEncodeTrackCommands_test()
{
    // action 1 + top-left (640,360) little endian + (0,0) marks a point pick.
    QCOMPARE(SiyiAi::encodeTrackPoint(640, 360),
             fromHex("55660109000000" "06" "018002680100000000" "ebfc"));
    // Nine payload bytes. A shorter frame leaves touch_rx/touch_ry outside DATA, and the
    // module reads them anyway - see the note on encodeCancelTracking.
    QCOMPARE(SiyiAi::encodeCancelTracking(),
             fromHex("55660109000000" "06" "000000000000000000" "a172"));

    const QByteArray box = SiyiAi::encodeTrackBox(100, 200, 300, 400);
    QCOMPARE(static_cast<quint8>(box.at(8)), static_cast<quint8>(1));
    QCOMPARE(static_cast<quint8>(box.at(9)) | (static_cast<quint8>(box.at(10)) << 8), 100);
    QCOMPARE(static_cast<quint8>(box.at(15)) | (static_cast<quint8>(box.at(16)) << 8), 400);
}

void SiyiProtocolTest::_aiParseTargetStream_test()
{
    QByteArray data;
    data.append(int16le(512));   // centre x
    data.append(int16le(300));   // centre y
    data.append(int16le(80));    // width
    data.append(int16le(160));   // height
    data.append(static_cast<char>(SiyiAi::TargetType::Person));
    data.append(static_cast<char>(SiyiAi::TrackingStatus::IntermittentLoss));

    const auto target = SiyiAi::parseTargetStream(data);
    QVERIFY(target.has_value());
    QCOMPARE(target->centreX, static_cast<quint16>(512));
    QCOMPARE(target->centreY, static_cast<quint16>(300));
    QCOMPARE(target->width, static_cast<quint16>(80));
    QCOMPARE(target->height, static_cast<quint16>(160));
    QCOMPARE(target->type, SiyiAi::TargetType::Person);
    QCOMPARE(target->status, SiyiAi::TrackingStatus::IntermittentLoss);

    // An AI frame travels through the shared decoder; the raw command byte identifies it.
    QByteArray buffer = makePacket(static_cast<quint8>(SiyiAi::CommandId::TargetStream), data);
    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(static_cast<quint8>(frames.at(0).commandId),
             static_cast<quint8>(SiyiAi::CommandId::TargetStream));
}

void SiyiProtocolTest::_aiParseRejectsShortPayloads_test()
{
    QVERIFY(!SiyiAi::parseTargetStream(QByteArray(9, '\0')).has_value());
    QVERIFY(!SiyiAi::parseEnabledFlag(QByteArray()).has_value());
    QVERIFY(!SiyiAi::parseTrackRequestResult(QByteArray()).has_value());
    QVERIFY(!SiyiAi::parseTargetStreamState(QByteArray()).has_value());
}

void SiyiProtocolTest::_aiObjectCountRequestBytes_test()
{
    // Object counting lives on the module's private link, so its requests are long frames:
    // 55 66 AA BB, CTRL 1, a four byte DATA_LEN, SEQ, CMD 0xD5, header CRC32, payload, frame
    // CRC32. Expected bytes computed outside this codebase from the layout and the non-reflected
    // CRC32, not from the encoder, so a change to either shows up here.
    const auto request = [](const QByteArray &payload) {
        return SiyiLongProtocol::encode(static_cast<quint8>(SiyiAi::PrivateCommandId::ObjectCount), payload);
    };
    const auto payload = [](SiyiAi::ObjectCountMode mode) {
        return QByteArray(1, static_cast<char>(mode));
    };

    QCOMPARE(request(QByteArray()), fromHex("5566aabb01000000000000d54157285b7a363646"));
    QCOMPARE(request(payload(SiyiAi::ObjectCountMode::Stop)),
             fromHex("5566aabb01010000000000d5503f7f1400ad7c6485"));
    QCOMPARE(request(payload(SiyiAi::ObjectCountMode::Start)),
             fromHex("5566aabb01010000000000d5503f7f14011a61a581"));
    QCOMPARE(request(fromHex("02020100")),
             fromHex("5566aabb01040000000000d5b2eab46202020100186883cc"));
    QCOMPARE(request(payload(SiyiAi::ObjectCountMode::ClassList)),
             fromHex("5566aabb01010000000000d5503f7f1403745a2788"));

    // The keep-alive that stops the module hanging up on us. No payload, no reply.
    QCOMPARE(SiyiLongProtocol::encode(static_cast<quint8>(SiyiAi::PrivateCommandId::KeepAlive)),
             fromHex("5566aabb01000000000000802d977a34b7ad40eb"));
}

void SiyiProtocolTest::_aiParseObjectCountReport_test()
{
    // {mode, model, class_count, tally per class}.
    const auto report = SiyiAi::parseObjectCountReport(fromHex("010504" "0200ff00"));
    QVERIFY(report.has_value());
    QVERIFY(report->counting);
    QCOMPARE(report->counts, QList<int>({2, 0, 255, 0}));

    // Counting off: two bytes, no class count and no tallies.
    const auto off = SiyiAi::parseObjectCountReport(fromHex("0005"));
    QVERIFY(off.has_value());
    QVERIFY(!off->counting);
    QVERIFY(off->counts.isEmpty());

    // The reply to a bare state query stops at the class count. Counting is on, but there is
    // nothing to display, and inventing zeroes for four classes would read as an empty street.
    const auto state = SiyiAi::parseObjectCountReport(fromHex("010504"));
    QVERIFY(state.has_value());
    QVERIFY(state->counting);
    QVERIFY(state->counts.isEmpty());

    // Class count and payload disagree: same treatment, no partial row.
    const auto truncated = SiyiAi::parseObjectCountReport(fromHex("010504" "0200"));
    QVERIFY(truncated.has_value());
    QVERIFY(truncated->counts.isEmpty());

    QVERIFY(!SiyiAi::parseObjectCountReport(QByteArray()).has_value());
}

void SiyiProtocolTest::_aiParseObjectClassNames_test()
{
    // {0x03, model, class_count, mask per class, comma separated names}.
    QByteArray reply = fromHex("030504" "01010101");
    reply.append("person,car,bus,truck");
    reply.append('\0');
    QCOMPARE(SiyiAi::parseObjectClassNames(reply),
             QStringList({QStringLiteral("person"), QStringLiteral("car"), QStringLiteral("bus"),
                          QStringLiteral("truck")}));

    // A separator ends the blob: the empty part it leaves is punctuation, not a class.
    QByteArray trailingComma = fromHex("030503" "010101");
    trailingComma.append("person,car,bus,");
    trailingComma.append('\0');
    QCOMPARE(SiyiAi::parseObjectClassNames(trailingComma),
             QStringList({QStringLiteral("person"), QStringLiteral("car"), QStringLiteral("bus")}));

    // The same blob shape for a module that leaves its last class unnamed. The empty part is a
    // slot here, and dropping it would leave the whole list unusable for the rest of the flight:
    // the tallies are positional, so a list short of the class count is thrown away, and the poll
    // only ever re-reads the same reply.
    QByteArray emptyLastName = fromHex("030503" "010101");
    emptyLastName.append("person,car,");
    emptyLastName.append('\0');
    QCOMPARE(SiyiAi::parseObjectClassNames(emptyLastName),
             QStringList({QStringLiteral("person"), QStringLiteral("car"), QString()}));

    // A list naming fewer classes than it counts cannot be indexed, and guessing which name went
    // missing is how the wrong slot ends up reported as people.
    QByteArray short_ = fromHex("030504" "01010101");
    short_.append("person,car");
    QVERIFY(SiyiAi::parseObjectClassNames(short_).isEmpty());

    // No name blob at all.
    QVERIFY(SiyiAi::parseObjectClassNames(fromHex("030504" "01010101")).isEmpty());

    // Not the class list reply.
    QVERIFY(SiyiAi::parseObjectClassNames(fromHex("010504" "0200ff00")).isEmpty());
    QVERIFY(SiyiAi::parseObjectClassNames(QByteArray()).isEmpty());
}

void SiyiProtocolTest::_speakerEncodeCommands_test()
{
    // The loudspeaker borrows the SIYI byte layout but not its STX: 0xA5 0x5A, because the
    // controller's RC MCU swallows 0x55 0x66 as its own SDK instead of passing it to the air unit.
    // Pinned here rather than read back from the encoder, so a header that drifts to 0x55 0x66 -
    // which builds, encodes and round-trips perfectly on this side while the payload never hears a
    // word - fails here instead of in the field.
    const QByteArray play = SpeakerProtocol::encodePlay(3);
    QCOMPARE(play.size(), 11);
    QCOMPARE(static_cast<quint8>(play.at(0)), static_cast<quint8>(0xA5));
    QCOMPARE(static_cast<quint8>(play.at(1)), static_cast<quint8>(0x5A));
    QCOMPARE(static_cast<quint8>(play.at(7)), static_cast<quint8>(SpeakerProtocol::CommandId::Play));
    QCOMPARE(static_cast<quint8>(play.at(8)), static_cast<quint8>(3));

    // A frame built here must survive the loudspeaker's own decoder unchanged. It cannot go
    // through SiyiProtocol::decode: that one syncs on 0x55 0x66 and drops this frame as garbage,
    // which is exactly what the different header is for.
    QByteArray buffer = play;
    const QList<SpeakerProtocol::Frame> frames = SpeakerProtocol::decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(static_cast<quint8>(frames.at(0).commandId),
             static_cast<quint8>(SpeakerProtocol::CommandId::Play));
    QVERIFY(buffer.isEmpty());

    // And the camera decoder must not claim it: the two codecs share one link on the controller.
    QByteArray crossFeed = play;
    QVERIFY(SiyiProtocol::decode(crossFeed).isEmpty());

    QCOMPARE(SpeakerProtocol::encodeStop().size(), 10);

    // Volume is clamped into the protocol's 0..100 range.
    QCOMPARE(static_cast<quint8>(SpeakerProtocol::encodeSetVolume(255).at(8)), static_cast<quint8>(100));
}

void SiyiProtocolTest::_speakerParseState_test()
{
    QByteArray data;
    data.append(static_cast<char>(1));    // playing
    data.append(static_cast<char>(3));    // track
    data.append(static_cast<char>(80));   // volume
    data.append(static_cast<char>(4));    // track count

    const auto state = SpeakerProtocol::parseState(data);
    QVERIFY(state.has_value());
    QVERIFY(state->playing);
    QCOMPARE(state->track, static_cast<quint8>(3));
    QCOMPARE(state->volume, static_cast<quint8>(80));
    QCOMPARE(state->trackCount, static_cast<quint8>(4));

    QVERIFY(!SpeakerProtocol::parseState(QByteArray(3, '\0')).has_value());
}

UT_REGISTER_TEST_LIGHTWEIGHT(SiyiProtocolTest, TestLabel::Unit)
