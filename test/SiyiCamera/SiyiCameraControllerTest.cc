#include "SiyiCameraControllerTest.h"

#include <QtNetwork/QUdpSocket>
#include <QtTest/QSignalSpy>

#include "Fact.h"
#include "SettingsManager.h"
#include "SiyiCameraController.h"
#include "SiyiCameraSettings.h"
#include "SiyiProtocol.h"

namespace {

/// Binds a fake gimbal on loopback, points the controller at it, and picks up the ephemeral port
/// the controller's own identity probe came from so replies can be addressed back to it.
void openFakeGimbal(QUdpSocket &gimbal, SiyiCameraController &controller, QHostAddress &address, quint16 &port)
{
    QVERIFY(gimbal.bind(QHostAddress::LocalHost, 0));

    SiyiCameraSettings *const settings = SettingsManager::instance()->siyiCameraSettings();
    QVERIFY(settings);
    settings->ipAddress()->setRawValue(QStringLiteral("127.0.0.1"));
    settings->port()->setRawValue(gimbal.localPort());

    controller.start();

    QVERIFY(gimbal.waitForReadyRead(TestTimeout::shortMs()));
    QByteArray probe;
    probe.resize(gimbal.pendingDatagramSize());
    QVERIFY(gimbal.readDatagram(probe.data(), probe.size(), &address, &port) > 0);
    QVERIFY(port != 0);
}

QList<SiyiProtocol::Frame> drainFrames(QUdpSocket &gimbal)
{
    QByteArray buffer;
    while (gimbal.hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(gimbal.pendingDatagramSize()), Qt::Uninitialized);
        (void) gimbal.readDatagram(datagram.data(), datagram.size());
        buffer.append(datagram);
    }
    return SiyiProtocol::decode(buffer);
}

QByteArray followReply(const QByteArray &data)
{
    return SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiProtocol::CommandId::AiFollow), data);
}

/// Answers the identity probe, giving the tests an ordering barrier: replies arrive in order, so
/// once the model shows up whatever was sent ahead of it has been processed too. ZT6 rather than
/// ZT30 keeps the poll from adding laser traffic.
QByteArray hardwareIdReply()
{
    return SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiProtocol::CommandId::AcquireHardwareId), QByteArray("83"));
}

} // namespace

/// The gimbal decides whether it can follow, not the GCS. Sending 0xC3 proves nothing on its own:
/// it is undocumented, and firmware that predates it answers nothing at all, so the switch may
/// only light on a reply. Anything else claims the pod is tracking the target when it is not.
void SiyiCameraControllerTest::_aiFollowNeedsTheGimbalToConfirm_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    // Not the singleton: it is only started on a normal app boot, and a private instance keeps
    // the fake gimbal's loopback port to this test.
    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    controller.setAiFollow(true);

    // Accumulated, not re-read: draining consumes, and the poll keeps unrelated commands coming.
    QList<QByteArray> followPayloads;
    const auto collectFollowPayloads = [&]() {
        for (const SiyiProtocol::Frame &frame : drainFrames(gimbal)) {
            if (frame.commandId == SiyiProtocol::CommandId::AiFollow) {
                followPayloads.append(frame.data);
            }
        }
        return followPayloads.size();
    };
    QTRY_VERIFY_WITH_TIMEOUT(collectFollowPayloads() == 1, TestTimeout::shortMs());
    QCOMPARE(followPayloads.at(0), QByteArray(1, '\1'));
    QVERIFY(!controller.aiFollowEnabled());

    // Nothing answers the 0xC3, so follow has to stay off even after the link has gone on
    // carrying traffic in both directions.
    const QByteArray barrier = hardwareIdReply();
    QCOMPARE(gimbal.writeDatagram(barrier, address, port), barrier.size());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.model().isEmpty(), TestTimeout::shortMs());

    QVERIFY(!controller.aiFollowEnabled());
    QCOMPARE(controller.aiFollowError(), static_cast<int>(SiyiCameraController::AiFollowError::None));
}

/// Every reason the gimbal gives has to reach the operator unchanged, and a code this build has
/// never seen must not be dressed up as one of the seven it can name - the command carries no
/// version negotiation, so a firmware update is free to add codes.
void SiyiCameraControllerTest::_aiFollowReportsEveryRefusalCode_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    // The gimbal only ever answers 0xC3 when it is asked, and a reply is now matched against what
    // was asked for, so the ask has to happen before any of these replies mean anything.
    controller.setAiFollow(true);

    QSignalSpy followSpy(&controller, &SiyiCameraController::aiFollowChanged);

    struct Reply {
        quint8 code;
        bool enabled;
        SiyiCameraController::AiFollowError error;
    };
    // Ordered so consecutive replies always differ, which is what makes the notify countable.
    const QList<Reply> replies = {
        {1, true, SiyiCameraController::AiFollowError::None},
        {2, false, SiyiCameraController::AiFollowError::TargetTooFarOrLow},
        {3, false, SiyiCameraController::AiFollowError::AiTrackingDisabled},
        {4, false, SiyiCameraController::AiFollowError::GpsDataMissing},
        {5, false, SiyiCameraController::AiFollowError::TargetTooCloseOrHigh},
        {6, false, SiyiCameraController::AiFollowError::InvertedModeUnsupported},
        {7, false, SiyiCameraController::AiFollowError::TargetNotSelected},
        {8, false, SiyiCameraController::AiFollowError::ModelUnsupported},
        {9, false, SiyiCameraController::AiFollowError::Unknown},
        {0, false, SiyiCameraController::AiFollowError::None},
        {255, false, SiyiCameraController::AiFollowError::Unknown},
    };

    int expectedChanges = 0;
    for (const Reply &reply : replies) {
        const QByteArray frame = followReply(QByteArray(1, static_cast<char>(reply.code)));
        QCOMPARE(gimbal.writeDatagram(frame, address, port), frame.size());
        ++expectedChanges;
        QVERIFY_SIGNAL_COUNT_WAIT(followSpy, expectedChanges, TestTimeout::shortMs());
        QCOMPARE(controller.aiFollowEnabled(), reply.enabled);
        QCOMPARE(controller.aiFollowError(), static_cast<int>(reply.error));
    }

    // A reply with no code in it at all leaves the last known state alone. Length beyond the first
    // byte is deliberately not checked - see the handler - so an empty payload is the only shape
    // that carries nothing to read.
    const QByteArray malformed = followReply(QByteArray());
    QCOMPARE(gimbal.writeDatagram(malformed, address, port), malformed.size());
    const QByteArray barrier = hardwareIdReply();
    QCOMPARE(gimbal.writeDatagram(barrier, address, port), barrier.size());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.model().isEmpty(), TestTimeout::shortMs());

    QCOMPARE(followSpy.count(), expectedChanges);
    QCOMPARE(controller.aiFollowError(), static_cast<int>(SiyiCameraController::AiFollowError::Unknown));
}

/// 0xC3 is undocumented and its reply length is not: UniGCS reads the first byte of whatever comes
/// back (e.java:732, if (i11 > 0)), and this pod family already answers a documented command with a
/// wider ack than the manual gives it. A gimbal that pads its reply must not read as one that never
/// answered, which would leave the switch off over a pod that is following.
void SiyiCameraControllerTest::_aiFollowAcceptsAWiderReply_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    controller.setAiFollow(true);

    QSignalSpy followSpy(&controller, &SiyiCameraController::aiFollowChanged);

    const QByteArray twoByteAck = followReply(QByteArray::fromHex("0100"));
    QCOMPARE(gimbal.writeDatagram(twoByteAck, address, port), twoByteAck.size());
    QVERIFY_SIGNAL_COUNT_WAIT(followSpy, 1, TestTimeout::shortMs());

    QVERIFY(controller.aiFollowEnabled());
    QCOMPARE(controller.aiFollowError(), static_cast<int>(SiyiCameraController::AiFollowError::None));
    QVERIFY(!controller.aiFollowStale());
}

/// The operator slides follow on, the gimbal takes its time, the operator changes their mind and
/// presses stop - and then the acceptance of the first request arrives. 0xC3 carries nothing that
/// ties a reply to a request, so acting on that acceptance puts the aircraft into GUIDED and kills
/// the sticks after an explicit cancel, with nothing on screen having been touched since.
void SiyiCameraControllerTest::_lateFollowAcceptanceAfterStopIsIgnored_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    controller.setAiFollow(true);
    controller.setAiFollow(false);

    // Both payloads really did go out; this is a late reply, not a send that never happened.
    QList<QByteArray> followPayloads;
    const auto collectFollowPayloads = [&]() {
        for (const SiyiProtocol::Frame &frame : drainFrames(gimbal)) {
            if (frame.commandId == SiyiProtocol::CommandId::AiFollow) {
                followPayloads.append(frame.data);
            }
        }
        return followPayloads.size();
    };
    QTRY_VERIFY_WITH_TIMEOUT(collectFollowPayloads() == 2, TestTimeout::shortMs());
    QCOMPARE(followPayloads.at(0), QByteArray(1, '\1'));
    QCOMPARE(followPayloads.at(1), QByteArray(1, '\0'));

    const QByteArray lateAcceptance = followReply(QByteArray(1, '\1'));
    QCOMPARE(gimbal.writeDatagram(lateAcceptance, address, port), lateAcceptance.size());

    // Ordering barrier: the identity reply is sent behind the acceptance, so once the model shows
    // up the acceptance has been through _handleFrame().
    const QByteArray barrier = hardwareIdReply();
    QCOMPARE(gimbal.writeDatagram(barrier, address, port), barrier.size());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.model().isEmpty(), TestTimeout::shortMs());

    // This is the whole point: the dashboard drives the flight mode off aiFollowEnabled, so it
    // must not come up after the cancel.
    QVERIFY(!controller.aiFollowEnabled());

    // And the state is not frozen either - asking again and being accepted still works.
    controller.setAiFollow(true);
    const QByteArray acceptance = followReply(QByteArray(1, '\1'));
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.aiFollowEnabled(), TestTimeout::shortMs());
}

/// The gimbal keeps flying the aircraft across a link drop. It has its own GPS and its own hold on
/// the target, and the SIYI air unit's setpoints never travelled on this socket, so two seconds of
/// tablet Wi-Fi trouble says nothing at all about whether follow is still on. Clearing follow on
/// that timeout put a grey "off" on the detection card and a "follow is off and the aircraft is in
/// GUIDED" banner on the map while the aircraft was in fact still chasing a person - and since
/// 0xC3 has no re-query, that lie stood for the rest of the flight.
void SiyiCameraControllerTest::_linkLossLeavesFollowUnconfirmedNotOff_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    // The link has to be up for the timeout to mean anything: _poll() only drops a connection it
    // believes in.
    const QByteArray barrier = hardwareIdReply();
    QCOMPARE(gimbal.writeDatagram(barrier, address, port), barrier.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), TestTimeout::shortMs());

    controller.setAiFollow(true);
    const QByteArray acceptance = followReply(QByteArray(1, '\1'));
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.aiFollowEnabled(), TestTimeout::shortMs());

    // Now the pod goes quiet. Nothing is answered, so the poll times the link out and resets the
    // pod state around it.
    QTRY_VERIFY_WITH_TIMEOUT(!controller.connected(), TestTimeout::mediumMs());

    QVERIFY(controller.aiFollowEnabled());   // still believed, because nothing said otherwise
    QVERIFY(controller.aiFollowStale());     // and honestly marked unconfirmed

    // The link comes back and the gimbal confirms again. The acceptance has to be honoured, which
    // it cannot be if the reset threw away what this side asked for.
    QCOMPARE(gimbal.writeDatagram(barrier, address, port), barrier.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), TestTimeout::shortMs());
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.aiFollowStale(), TestTimeout::shortMs());
    QVERIFY(controller.aiFollowEnabled());
}

/// Stop is the only way back to the sticks, and it is one datagram on a link with no
/// retransmission. Two things have to hold. It has to free the start slider, whose visible is
/// driven by aiFollowEnabled - waiting for a reply that a silent gimbal never sends, or answers
/// with a code the request guard drops, left the operator unable to re-engage follow for the rest
/// of the session. And it has to go out more than once, the way cancelTracking() already does for
/// the cheaper of the two commands.
void SiyiCameraControllerTest::_stopFollowFreesTheSwitchAndIsRepeated_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    controller.setAiFollow(true);
    const QByteArray acceptance = followReply(QByteArray(1, '\1'));
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.aiFollowEnabled(), TestTimeout::shortMs());

    QList<QByteArray> stopPayloads;
    const auto collectStops = [&]() {
        for (const SiyiProtocol::Frame &frame : drainFrames(gimbal)) {
            if ((frame.commandId == SiyiProtocol::CommandId::AiFollow) && (frame.data == QByteArray(1, '\0'))) {
                stopPayloads.append(frame.data);
            }
        }
        return stopPayloads.size();
    };
    (void) collectStops();

    // The gimbal answers nothing at all from here on, which is the case that used to strand the
    // operator.
    controller.setAiFollow(false);
    QVERIFY(!controller.aiFollowEnabled());
    QVERIFY(!controller.aiFollowStale());
    QCOMPARE(controller.aiFollowStopState(),
             static_cast<int>(SiyiCameraController::AiFollowStop::StopPending));

    // Repeated, not fired once and forgotten - and bounded, so it cannot run for the flight.
    QTRY_VERIFY_WITH_TIMEOUT(collectStops() >= 3, TestTimeout::mediumMs());

    // Spent, and the fact that nothing ever answered survives. The whole run takes 1.5 s, and
    // going quiet at the end of it - which is what a plain "pending" flag did - makes a stop whose
    // every datagram died on a two second Wi-Fi drop look exactly like a stop the gimbal took,
    // while it goes on flying the aircraft.
    QTRY_COMPARE_WITH_TIMEOUT(controller.aiFollowStopState(),
                              static_cast<int>(SiyiCameraController::AiFollowStop::StopUnconfirmed),
                              TestTimeout::mediumMs());
    const int sent = stopPayloads.size();
    QTest::qWait(400);
    QCOMPARE(collectStops(), sent);
    QCOMPARE(controller.aiFollowStopState(),
             static_cast<int>(SiyiCameraController::AiFollowStop::StopUnconfirmed));

    // And follow can be asked for again, which is the whole point of not gating the slider on a
    // flag only the gimbal can clear. The unresolved stop belongs to the request that is over.
    QSignalSpy restartSpy(&controller, &SiyiCameraController::aiFollowChanged);
    controller.setAiFollow(true);
    QCOMPARE(controller.aiFollowStopState(),
             static_cast<int>(SiyiCameraController::AiFollowStop::StopIdle));
    QCOMPARE(restartSpy.count(), 1);   // the binding behind the panel text has to be told
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.aiFollowEnabled(), TestTimeout::shortMs());
}

/// Nothing on this side has ever seen the gimbal's follow state when the app starts. The gimbal
/// keeps following across a QGC restart - the tablet can be OOM-killed mid-sortie - and across a
/// follow the hand controller started, and 0xC3 is never re-queried, so a false "off" here is not
/// self-correcting for the rest of the flight: the detection card paints a confident grey "off",
/// GUIDED banner stays silent, and the aircraft is chasing a person with dead sticks.
void SiyiCameraControllerTest::_followStartsUnconfirmedNotOff_test()
{
    SiyiCameraController controller(nullptr);

    // Before any link at all.
    QVERIFY(!controller.aiFollowEnabled());
    QVERIFY(controller.aiFollowStale());

    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;
    openFakeGimbal(gimbal, controller, address, port);

    // The link comes up and the pod identifies itself, which is all a gimbal that was already
    // following would volunteer - 0xC3 speaks only when asked.
    const QByteArray barrier = hardwareIdReply();
    QCOMPARE(gimbal.writeDatagram(barrier, address, port), barrier.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), TestTimeout::shortMs());

    QVERIFY(!controller.aiFollowEnabled());
    QVERIFY(controller.aiFollowStale());   // "unconfirmed", not "off"

    // And it is a starting state, not a stuck one: the first answer clears it.
    controller.setAiFollow(true);
    const QByteArray acceptance = followReply(QByteArray(1, '\1'));
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.aiFollowEnabled(), TestTimeout::shortMs());
    QVERIFY(!controller.aiFollowStale());
}

/// Closing the camera link must not leave the gimbal flying the aircraft, and a stop pressed after
/// it must not claim to be on its way.
///
/// stop() runs when the operator unchecks the camera setting or edits its address, and on the
/// destructor when QGC quits. It already turns the laser off for the reason that the pod keeps its
/// state when a client goes away; nothing in the recovered protocol says follow is any different.
/// Afterwards the follow panel is still reachable - its button is latched open on purpose so the
/// stop can never be gated away - but the socket and the poll behind it are gone, so a stop press
/// sends nothing at all and no repeat is ever coming.
void SiyiCameraControllerTest::_closingTheLinkStopsFollowAndSaysWhatItCouldNotSend_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    controller.setAiFollow(true);
    const QByteArray acceptance = followReply(QByteArray(1, '\1'));
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.aiFollowEnabled(), TestTimeout::shortMs());

    // Only follow-off frames are counted: the enable that set this up is still in flight on
    // loopback and would otherwise be mistaken for the frame under test.
    QList<QByteArray> stopPayloads;
    const auto collectStops = [&]() {
        for (const SiyiProtocol::Frame &frame : drainFrames(gimbal)) {
            if ((frame.commandId == SiyiProtocol::CommandId::AiFollow) && (frame.data == QByteArray(1, '\0'))) {
                stopPayloads.append(frame.data);
            }
        }
        return stopPayloads.size();
    };
    (void) collectStops();

    // Closing the link takes follow with it, next to the laser and for the same reason.
    controller.stop();
    QTRY_VERIFY_WITH_TIMEOUT(collectStops() >= 1, TestTimeout::shortMs());

    // Follow reads as off, which is what lights the dashboard's "follow is off and the aircraft is
    // still in GUIDED" banner - that branch is deliberately not gated on staleness, so clearing it
    // here buys the banner nothing.
    QVERIFY(!controller.aiFollowEnabled());

    // Staleness stays. That 0xC3 went out on UDP with the socket torn down in the same function,
    // so this is the one stop path with no repeat and nothing left to hear an answer on. The
    // operator's own stop shows "unconfirmed" off exactly this evidence after three tries; the
    // path that tries once cannot be the confident one. The detection card draws grey
    // "unconfirmed" rather than a settled "off" while the gimbal may still be flying the aircraft.
    QVERIFY(controller.aiFollowStale());
    QCOMPARE(controller.aiFollowStopState(),
             static_cast<int>(SiyiCameraController::AiFollowStop::StopUnconfirmed));

    // Now the operator presses stop in the panel that is still open. Nothing can go out, and
    // nothing will: the poll that carries the repeats is stopped too.
    const int beforePress = stopPayloads.size();
    controller.setAiFollow(false);
    QCOMPARE(controller.aiFollowStopState(),
             static_cast<int>(SiyiCameraController::AiFollowStop::StopUnsent));

    QTest::qWait(400);
    QCOMPARE(collectStops(), beforePress);
    QCOMPARE(controller.aiFollowStopState(),
             static_cast<int>(SiyiCameraController::AiFollowStop::StopUnsent));
}

/// A follow request nobody has answered is not "off".
///
/// A confirmed stop is the one thing that leaves this side entitled to say "off": aiFollowStale
/// goes false, and that is what turns the detection card's follow slot from "unconfirmed" into a
/// grey "off". Nothing raises it again by itself - 0xC3 is answered only when asked - so the next
/// request inherited that "off" and kept it for as long as the gimbal stayed silent. A gimbal too
/// old to answer 0xC3 reproduces it every time: the operator slides follow on, the card says the
/// aircraft is not following, and if the gimbal did take the request the aircraft is flying itself.
void SiyiCameraControllerTest::_newFollowRequestIsUnconfirmedNotOff_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    // Start, confirmed. Then stop, which is the step that earns the confident "off".
    controller.setAiFollow(true);
    const QByteArray acceptance = followReply(QByteArray(1, '\1'));
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.aiFollowEnabled(), TestTimeout::shortMs());

    controller.setAiFollow(false);
    QVERIFY(!controller.aiFollowEnabled());
    QVERIFY(!controller.aiFollowStale());

    // The operator slides follow on again, and this gimbal never answers 0xC3.
    QSignalSpy spy(&controller, &SiyiCameraController::aiFollowChanged);
    controller.setAiFollow(true);

    QVERIFY(!controller.aiFollowEnabled());     // nothing has confirmed it
    QVERIFY(controller.aiFollowStale());        // so "unconfirmed", not "off"
    QVERIFY(spy.count() >= 1);                  // and the card's binding has to hear about it
    QCOMPARE(controller.aiFollowStopState(),
             static_cast<int>(SiyiCameraController::AiFollowStop::StopIdle));

    // Still a starting state and not a stuck one.
    QCOMPARE(gimbal.writeDatagram(acceptance, address, port), acceptance.size());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.aiFollowStale(), TestTimeout::shortMs());
    QVERIFY(controller.aiFollowEnabled());
}

/// A refusal belongs to the request that was refused. The gimbal answers 0xC3 only when it feels
/// like it - old firmware never answers at all - so a reason left standing across a fresh request
/// is read as the reason this one was refused: the operator fixes their GPS fix, slides again,
/// gets silence, and is told once more that GPS data is missing. The panel's "the gimbal has to
/// confirm it, and if it does not in a few seconds it is not answering" line is gated on there
/// being no error, so the sentence that would have explained the silence is the one suppressed.
void SiyiCameraControllerTest::_newFollowRequestDropsTheOldRefusal_test()
{
    QUdpSocket gimbal;
    QHostAddress address;
    quint16 port = 0;

    SiyiCameraController controller(nullptr);
    openFakeGimbal(gimbal, controller, address, port);

    controller.setAiFollow(true);
    const QByteArray refusal = followReply(
        QByteArray(1, static_cast<char>(SiyiCameraController::AiFollowError::GpsDataMissing)));
    QCOMPARE(gimbal.writeDatagram(refusal, address, port), refusal.size());
    QTRY_COMPARE_WITH_TIMEOUT(controller.aiFollowError(),
                              static_cast<int>(SiyiCameraController::AiFollowError::GpsDataMissing),
                              TestTimeout::shortMs());

    QSignalSpy followSpy(&controller, &SiyiCameraController::aiFollowChanged);
    controller.setAiFollow(true);

    QCOMPARE(controller.aiFollowError(), static_cast<int>(SiyiCameraController::AiFollowError::None));
    QCOMPARE(followSpy.count(), 1);   // and the panel is told, or the binding keeps the old text
}

UT_REGISTER_TEST(SiyiCameraControllerTest, TestLabel::Unit)
