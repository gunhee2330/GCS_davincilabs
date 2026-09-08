#pragma once

#include "UnitTest.h"

class SiyiProtocolTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _encodeMatchesDocumentedFrames_test();
    void _encodeGimbalRotationClampsRates_test();
    void _encodeAbsoluteZoomSplitsMultiple_test();
    void _encodeThermalRangeRequest_test();
    void _encodeManualZoomEncodesNegativeAsUnsigned_test();
    void _decodeExtractsFrame_test();
    void _decodeSkipsLeadingGarbage_test();
    void _decodeRejectsBadCrc_test();
    void _decodeKeepsPartialFrameBuffered_test();
    void _decodeHandlesCoalescedFrames_test();
    void _parseAttitude_test();
    void _parseConfigInfo_test();
    void _parseHardwareModel_test();
    void _parseRangefinderTarget_test();
    void _parseRangefinderDistance_test();
    void _laserStateCodec_test();
    void _aiFollowCodec_test();
    void _parseRejectsShortPayloads_test();
    void _aiEncodeMatchesFraming_test();
    void _aiEncodeTrackCommands_test();
    void _aiParseTargetStream_test();
    void _aiParseRejectsShortPayloads_test();
    void _aiObjectCountRequestBytes_test();
    void _aiParseObjectCountReport_test();
    void _aiParseObjectClassNames_test();
    void _speakerEncodeCommands_test();
    void _speakerParseState_test();
};
