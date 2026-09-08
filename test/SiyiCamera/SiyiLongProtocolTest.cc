#include "SiyiLongProtocolTest.h"

#include "SiyiLongProtocol.h"

using namespace SiyiLongProtocol;

namespace {

QByteArray fromHex(const char *hex)
{
    return QByteArray::fromHex(QByteArray(hex));
}

void appendUint32(QByteArray &out, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.append(static_cast<char>((value >> shift) & 0xFF));
    }
}

/// Builds a well-formed frame without going through encode(), so decode() is exercised against
/// an independent construction. CTRL defaults to 0x02, which is what the module sets on the
/// replies and pushes this decoder actually sees.
QByteArray makeFrame(quint8 commandId, const QByteArray &data, quint16 sequence = 0, quint8 ctrl = 0x02)
{
    QByteArray frame = fromHex("5566aabb");
    frame.append(static_cast<char>(ctrl));
    appendUint32(frame, static_cast<quint32>(data.size()));
    frame.append(static_cast<char>(sequence & 0xFF));
    frame.append(static_cast<char>((sequence >> 8) & 0xFF));
    frame.append(static_cast<char>(commandId));
    appendUint32(frame, crc32(frame));
    frame.append(data);
    appendUint32(frame, crc32(frame));
    return frame;
}

} // namespace

void SiyiLongProtocolTest::_crc32MatchesIndependentVectors_test()
{
    // Computed outside this codebase, from the algorithm rather than from encode(): polynomial
    // 0x04C11DB7, initial value 0, non-reflected, no final xor. Cross-checked byte for byte
    // against the 256 entry table the SIYI app ships
    // (biz/siyi/protocol/bu/manufacturer/siyi/j.java:19, driven by z.java:141).
    //
    // These literals are the point of this test: qChecksum and zlib's crc32 are the reflected
    // variant of the same polynomial and answer differently, so swapping this codec for either
    // of them - the obvious "simplification" for whoever reads this next - fails here instead of
    // silently producing frames the module drops.
    QCOMPARE(crc32(QByteArray()), 0x00000000U);
    QCOMPARE(crc32(QByteArray("123456789")), 0x89A1897FU);
    QCOMPARE(crc32(fromHex("5566aabb")), 0xD192448AU);

    QByteArray ascending;
    for (int index = 0; index < 20; ++index) {
        ascending.append(static_cast<char>(index));
    }
    QCOMPARE(crc32(ascending), 0x2B2D0AB4U);
}

void SiyiLongProtocolTest::_encodeMatchesFrameLayout_test()
{
    // Whole frames generated independently of this codec from the layout in
    // long_protocol_tcp_server_send@0x560b10: magic, CTRL 0x01 (need_ack), DATA_LEN over four
    // little endian bytes, SEQ, CMD_ID, header CRC32, payload, whole frame CRC32. They pin the
    // field order and the little endian placement of both checksums at once.
    QCOMPARE(encode(0xD5, QByteArray(1, '\1'), 1), fromHex("5566aabb01010000000100d5" "d793a715" "01" "4f3048be"));
    QCOMPARE(encode(0x80), fromHex("5566aabb01000000000000802d977a34b7ad40eb"));
}

void SiyiLongProtocolTest::_encodeEmptyPayload_test()
{
    // The 0xD5 status query and the 0x80 keepalive both carry no payload. DATA_LEN 0 still has
    // to produce a full 20 byte frame carrying both checksums.
    const QByteArray frame = encode(0xD5);
    QCOMPARE(frame, fromHex("5566aabb01000000000000d54157285b7a363646"));
    QCOMPARE(frame.size(), 20);

    QByteArray buffer = frame;
    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).commandId, static_cast<quint8>(0xD5));
    QVERIFY(frames.at(0).data.isEmpty());
    QVERIFY(buffer.isEmpty());
}

void SiyiLongProtocolTest::_decodeRoundTrip_test()
{
    const QByteArray payload = fromHex("0100500102030405");
    QByteArray buffer = encode(0xD5, payload, 0x1234);

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).commandId, static_cast<quint8>(0xD5));
    QCOMPARE(frames.at(0).sequence, static_cast<quint16>(0x1234));
    QCOMPARE(frames.at(0).data, payload);
    QVERIFY(!frames.at(0).isAck);
    QVERIFY(buffer.isEmpty());
}

void SiyiLongProtocolTest::_decodeReassemblesFrameSplitByteByByte_test()
{
    // TCP hands over arbitrary slices, and a 0xD5 push is small enough to be split by any of
    // them. Fed one byte at a time the frame must appear exactly once, on the byte that
    // completes it, and never twice.
    const QByteArray payload = fromHex("01005003040506");
    const QByteArray frame = makeFrame(0xD5, payload, 9);

    QByteArray buffer;
    int completed = 0;
    for (int index = 0; index < frame.size(); ++index) {
        buffer.append(frame.at(index));
        const QList<Frame> frames = decode(buffer);
        completed += static_cast<int>(frames.size());
        if (index < (frame.size() - 1)) {
            QCOMPARE(frames.size(), 0);
        } else {
            QCOMPARE(frames.size(), 1);
            QCOMPARE(frames.at(0).data, payload);
            QVERIFY(frames.at(0).isAck);
        }
    }
    QCOMPARE(completed, 1);
    QVERIFY(buffer.isEmpty());
}

void SiyiLongProtocolTest::_decodeHandlesCoalescedFrames_test()
{
    QByteArray buffer = makeFrame(0xD5, fromHex("010005"), 1);
    buffer.append(makeFrame(0x80, QByteArray(), 2));
    buffer.append(fromHex("5566"));   // A third frame that has only started to arrive.

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).commandId, static_cast<quint8>(0xD5));
    QCOMPARE(frames.at(0).sequence, static_cast<quint16>(1));
    QCOMPARE(frames.at(1).commandId, static_cast<quint8>(0x80));
    QCOMPARE(buffer, fromHex("5566"));
}

void SiyiLongProtocolTest::_decodeSkipsLeadingGarbage_test()
{
    // What a reconnect looks like: the module never clears its transmit ring when a client goes
    // away, so the stream can open on the tail of a frame meant for the previous one. The
    // garbage here includes a near miss on the magic (55 66 AA 00) to make sure resynchronising
    // does not stop at the first two bytes.
    QByteArray buffer = fromHex("aa55bb5566aa00ff");
    buffer.append(makeFrame(0xD5, fromHex("010004"), 3));

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).data, fromHex("010004"));
    QVERIFY(buffer.isEmpty());
}

void SiyiLongProtocolTest::_decodeRecoversFromCorruptCrc_test()
{
    // Two different corruptions: one that breaks the header CRC, one that leaves the header
    // intact but breaks the whole frame CRC. Neither may take the next good frame down with it.
    QByteArray corruptHeader = makeFrame(0xD5, fromHex("010004"), 4);
    corruptHeader[12] = static_cast<char>(corruptHeader.at(12) ^ 0xFF);

    QByteArray corruptPayload = makeFrame(0xD5, fromHex("010004"), 5);
    corruptPayload[16] = static_cast<char>(corruptPayload.at(16) ^ 0xFF);

    QByteArray buffer = corruptHeader;
    buffer.append(corruptPayload);
    buffer.append(makeFrame(0x80, QByteArray(), 6));

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).commandId, static_cast<quint8>(0x80));
    QCOMPARE(frames.at(0).sequence, static_cast<quint16>(6));
    QVERIFY(buffer.isEmpty());
}

void SiyiLongProtocolTest::_decodeRejectsAbsurdDataLength_test()
{
    // A header whose CRC is perfectly good but which claims a 2 GB payload. It has to be
    // rejected on the length alone: nothing may reserve memory from a field this link gives us
    // no way to sanity check, and the decoder must not sit waiting for bytes that never come.
    QByteArray header = fromHex("5566aabb02");
    appendUint32(header, 0x7FFFFFFFU);
    header.append(fromHex("0000d5"));
    QCOMPARE(header.size(), 12);

    QByteArray buffer = header;
    appendUint32(buffer, crc32(header));
    buffer.append(makeFrame(0x80, QByteArray(), 7));

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).commandId, static_cast<quint8>(0x80));
    QVERIFY(buffer.isEmpty());
}

void SiyiLongProtocolTest::_decodeBoundsLeftoverBuffer_test()
{
    // Bytes that never resynchronise are consumed rather than accumulated, so a peer sending
    // nothing but rubbish cannot grow the caller's buffer without bound.
    QByteArray buffer(64 * 1024, '\x55');

    const QList<Frame> frames = decode(buffer);
    QCOMPARE(frames.size(), 0);
    QVERIFY(buffer.size() < 20);
}

UT_REGISTER_TEST_LIGHTWEIGHT(SiyiLongProtocolTest, TestLabel::Unit)
