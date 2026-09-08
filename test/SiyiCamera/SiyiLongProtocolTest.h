#pragma once

#include "UnitTest.h"

class SiyiLongProtocolTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _crc32MatchesIndependentVectors_test();
    void _encodeMatchesFrameLayout_test();
    void _encodeEmptyPayload_test();
    void _decodeRoundTrip_test();
    void _decodeReassemblesFrameSplitByteByByte_test();
    void _decodeHandlesCoalescedFrames_test();
    void _decodeSkipsLeadingGarbage_test();
    void _decodeRecoversFromCorruptCrc_test();
    void _decodeRejectsAbsurdDataLength_test();
    void _decodeBoundsLeftoverBuffer_test();
};
