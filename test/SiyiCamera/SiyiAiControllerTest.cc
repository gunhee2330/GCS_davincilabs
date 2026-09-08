#include "SiyiAiControllerTest.h"

#include <QtCore/QRegularExpression>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>
#include <QtTest/QSignalSpy>

#include "Fact.h"
#include "SettingsManager.h"
#include "SiyiAiController.h"
#include "SiyiAiProtocol.h"
#include "SiyiCameraSettings.h"
#include "SiyiLongProtocol.h"
#include "SiyiProtocol.h"

namespace {

QByteArray uint16le(quint16 value)
{
    QByteArray out;
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    return out;
}

/// One CommandId::TargetStream frame as the module emits it: the box in the reference frame,
/// its class, then the tracking status this test varies.
QByteArray targetStreamFrame(SiyiAi::TrackingStatus status)
{
    QByteArray data;
    data.append(uint16le(640));    // centre x
    data.append(uint16le(360));    // centre y
    data.append(uint16le(80));     // width
    data.append(uint16le(160));    // height
    data.append(static_cast<char>(SiyiAi::TargetType::Person));
    data.append(static_cast<char>(status));
    return SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiAi::CommandId::TargetStream), data);
}

/// Binds a fake module on loopback, points the controller at it, and picks up the ephemeral port
/// the controller's own announcement came from so replies can be addressed back to it.
void openFakeModule(QUdpSocket &module, SiyiAiController &controller, QHostAddress &address, quint16 &port)
{
    QVERIFY(module.bind(QHostAddress::LocalHost, 0));

    SiyiCameraSettings *const settings = SettingsManager::instance()->siyiCameraSettings();
    QVERIFY(settings);
    settings->aiIpAddress()->setRawValue(QStringLiteral("127.0.0.1"));
    settings->aiPort()->setRawValue(module.localPort());

    controller.start();

    QVERIFY(module.waitForReadyRead(TestTimeout::shortMs()));
    QByteArray probe;
    probe.resize(module.pendingDatagramSize());
    QVERIFY(module.readDatagram(probe.data(), probe.size(), &address, &port) > 0);
    QVERIFY(port != 0);
}

/// Everything the fake module has been sent, in order.
QList<SiyiProtocol::Frame> drainFrames(QUdpSocket &module)
{
    QByteArray buffer;
    while (module.hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(module.pendingDatagramSize()), Qt::Uninitialized);
        (void) module.readDatagram(datagram.data(), datagram.size());
        buffer.append(datagram);
    }
    return SiyiProtocol::decode(buffer);
}

/// Command ids of everything the fake module has been sent, in order.
QList<quint8> drainCommands(QUdpSocket &module)
{
    QList<quint8> commands;
    for (const SiyiProtocol::Frame &frame : drainFrames(module)) {
        commands.append(static_cast<quint8>(frame.commandId));
    }
    return commands;
}

/// How many of @a frames are cancels. A selection and a cancel are the same command id and differ
/// only in the leading track_action byte, so counting by command id would count both.
int cancelCount(const QList<SiyiProtocol::Frame> &frames)
{
    int count = 0;
    for (const SiyiProtocol::Frame &frame : frames) {
        if ((static_cast<quint8>(frame.commandId) == static_cast<quint8>(SiyiAi::CommandId::SetTrackTarget)) &&
            !frame.data.isEmpty() && (frame.data.at(0) == '\0')) {
            ++count;
        }
    }
    return count;
}

/// The mirror of cancelCount(): track_action 1, a target being picked.
int selectionCount(const QList<SiyiProtocol::Frame> &frames)
{
    int count = 0;
    for (const SiyiProtocol::Frame &frame : frames) {
        if ((static_cast<quint8>(frame.commandId) == static_cast<quint8>(SiyiAi::CommandId::SetTrackTarget)) &&
            !frame.data.isEmpty() && (frame.data.at(0) == '\1')) {
            ++count;
        }
    }
    return count;
}

/// An ObjectCountMode::ClassList reply naming the module's classes in its own order.
SiyiLongProtocol::Frame classListFrame(const QStringList &names, quint8 model = 5)
{
    QByteArray data;
    data.append(static_cast<char>(SiyiAi::ObjectCountMode::ClassList));
    data.append(static_cast<char>(model));
    data.append(static_cast<char>(names.size()));
    data.append(QByteArray(names.size(), '\1'));            // filter mask, all classes counted
    data.append(names.join(QLatin1Char(',')).toLatin1());
    data.append('\0');

    SiyiLongProtocol::Frame frame;
    frame.commandId = static_cast<quint8>(SiyiAi::PrivateCommandId::ObjectCount);
    frame.isAck = true;
    frame.data = data;
    return frame;
}

/// An unsolicited count push: one unsigned byte per class, in the same order.
SiyiLongProtocol::Frame countPushFrame(const QList<int> &counts, quint8 model = 5)
{
    QByteArray data;
    data.append(static_cast<char>(SiyiAi::ObjectCountMode::Start));
    data.append(static_cast<char>(model));
    data.append(static_cast<char>(counts.size()));
    for (const int count : counts) {
        data.append(static_cast<char>(count));
    }

    SiyiLongProtocol::Frame frame;
    frame.commandId = static_cast<quint8>(SiyiAi::PrivateCommandId::ObjectCount);
    frame.isAck = true;
    frame.data = data;
    return frame;
}

/// The module's answer to a tracking-state query, carrying the sequence it is an answer to.
/// Handed to _handleFrame() directly rather than written to the socket: which reply is consumed by
/// which cancel is the whole subject, and a datagram in flight would leave that to the scheduler.
SiyiProtocol::Frame trackingStateFrame(quint8 tracking, quint16 sequence)
{
    SiyiProtocol::Frame frame;
    frame.commandId = static_cast<SiyiProtocol::CommandId>(SiyiAi::CommandId::RequestTrackingState);
    frame.sequence = sequence;
    frame.isAck = true;
    frame.data = QByteArray(1, static_cast<char>(tracking));
    return frame;
}

/// The sequence of the last tracking-state query in @a frames, or -1 if there is none.
int lastTrackingQuery(const QList<SiyiProtocol::Frame> &frames)
{
    int sequence = -1;
    for (const SiyiProtocol::Frame &frame : frames) {
        if (static_cast<quint8>(frame.commandId) == static_cast<quint8>(SiyiAi::CommandId::RequestTrackingState)) {
            sequence = frame.sequence;
        }
    }
    return sequence;
}

/// The module's answer to a bare state query while counting is off: the mode byte and the model,
/// and no tally row at all.
SiyiLongProtocol::Frame countingOffFrame()
{
    QByteArray data;
    data.append(static_cast<char>(SiyiAi::ObjectCountMode::Stop));
    data.append(static_cast<char>(5));                      // model id

    SiyiLongProtocol::Frame frame;
    frame.commandId = static_cast<quint8>(SiyiAi::PrivateCommandId::ObjectCount);
    frame.isAck = true;
    frame.data = data;
    return frame;
}

/// A connected loopback pair for the count link. The controller's own link is opened by _poll()
/// against the module's fixed private port, which a test cannot stand in for; the caller hands the
/// near end to the controller instead, which leaves everything downstream of it - _sendCount() and
/// its not-connected early-out - exactly as shipped. Parented to the controller so its stop()
/// disposes of the socket the way it disposes of its own.
QTcpSocket *openFakeCountLink(SiyiAiController &controller, QTcpServer &server, QTcpSocket *&farEnd)
{
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return nullptr;
    }

    auto *const nearEnd = new QTcpSocket(&controller);
    nearEnd->connectToHost(QHostAddress::LocalHost, server.serverPort());
    if (!nearEnd->waitForConnected(TestTimeout::shortMs()) ||
        !server.waitForNewConnection(TestTimeout::shortMs())) {
        return nullptr;
    }

    farEnd = server.nextPendingConnection();
    return nearEnd;
}

/// Everything the controller has written to the count link since the last call. QTcpSocket buffers
/// a write until the event loop runs, and these tests drive the controller by direct call rather
/// than by spinning one, so the near end has to be flushed by hand or the far end sees nothing.
QList<SiyiLongProtocol::Frame> drainCountFrames(QTcpSocket *nearEnd, QTcpSocket *farEnd)
{
    (void) nearEnd->flush();
    (void) nearEnd->waitForBytesWritten(TestTimeout::shortMs());

    QByteArray buffer;
    while (farEnd->waitForReadyRead(20)) {
        buffer.append(farEnd->readAll());
    }
    buffer.append(farEnd->readAll());
    return SiyiLongProtocol::decode(buffer);
}

/// The ObjectCount writes among what the controller has sent. The keep-alive (CMD 0x80) is
/// dropped: it is the link's heartbeat and says nothing about counting.
QList<SiyiLongProtocol::Frame> objectCountWrites(QTcpSocket *nearEnd, QTcpSocket *farEnd)
{
    QList<SiyiLongProtocol::Frame> counts;
    for (const SiyiLongProtocol::Frame &frame : drainCountFrames(nearEnd, farEnd)) {
        if (frame.commandId == static_cast<quint8>(SiyiAi::PrivateCommandId::ObjectCount)) {
            counts.append(frame);
        }
    }
    return counts;
}

/// Poll ticks in one keep-alive window. kCountKeepAliveInterval is 20, so 20 ticks is one window
/// and exactly one visit to the branch these tests are about.
constexpr int kKeepAliveWindowTicks = 20;

} // namespace

/// A cancel from the hand controller or SIYI's own app reaches the GCS only as a target stream
/// carrying Track_Sta 3. It has to clear the tracking state at once, otherwise the dashboard
/// keeps reporting a target and drawing its stale box until the stream times out.
void SiyiAiControllerTest::_cancelledTargetStopsTracking_test()
{
    QUdpSocket module;
    QHostAddress controllerAddress;
    quint16 controllerPort = 0;

    // Not the singleton: it is only started on a normal app boot, and a private instance keeps
    // the fake module's loopback port to this test.
    SiyiAiController controller(nullptr);
    openFakeModule(module, controller, controllerAddress, controllerPort);

    QSignalSpy targetSpy(&controller, &SiyiAiController::targetChanged);

    const QByteArray tracking = targetStreamFrame(SiyiAi::TrackingStatus::Tracking);
    QCOMPARE(module.writeDatagram(tracking, controllerAddress, controllerPort), tracking.size());
    QVERIFY_SIGNAL_COUNT_WAIT(targetSpy, 1, TestTimeout::shortMs());
    QVERIFY(controller.hasTarget());
    QVERIFY(!controller.targetLost());

    const QByteArray cancelled = targetStreamFrame(SiyiAi::TrackingStatus::CancelledByUser);
    QCOMPARE(module.writeDatagram(cancelled, controllerAddress, controllerPort), cancelled.size());
    QVERIFY_SIGNAL_COUNT_WAIT(targetSpy, 2, TestTimeout::shortMs());
    QVERIFY(!controller.hasTarget());
}

/// Cancel asks the module whether it still holds a selection before sending one. A cancel that
/// arrives when nothing is selected does not clear anything - the module falls into its
/// tap-selection branch and arms a fresh selection instead - and it drops that flag by itself on
/// an automatic loss, which is precisely when an operator reaches for cancel.
void SiyiAiControllerTest::_cancelAsksTheModuleFirst_test()
{
    QUdpSocket module;
    QHostAddress controllerAddress;
    quint16 controllerPort = 0;

    SiyiAiController controller(nullptr);
    openFakeModule(module, controller, controllerAddress, controllerPort);
    (void) drainCommands(module);

    const auto replyTrackingState = [&](quint8 state) {
        const QByteArray reply = SiyiProtocol::encodeRaw(
            static_cast<quint8>(SiyiAi::CommandId::RequestTrackingState), QByteArray(1, static_cast<char>(state)));
        QCOMPARE(module.writeDatagram(reply, controllerAddress, controllerPort), reply.size());
    };

    // Accumulated and matched by content, not compared exactly. start() sends two frames back to
    // back and the drain above only clears what the kernel had already queued, so the second one
    // lands in this drain often enough to make an exact compare a coin toss - measured at five
    // failures in ten runs. The claim being made here is narrower anyway: the query goes out, and
    // no cancel goes out with it.
    controller.cancelTracking();
    QList<quint8> sent;
    QTRY_VERIFY_WITH_TIMEOUT((sent += drainCommands(module))
                                 .contains(static_cast<quint8>(SiyiAi::CommandId::RequestTrackingState)),
                             TestTimeout::shortMs());
    QVERIFY(!sent.contains(static_cast<quint8>(SiyiAi::CommandId::SetTrackTarget)));

    // Nothing selected: no cancel goes out at all. The recognition frame behind it is an
    // ordering barrier - replies arrive in order, so once its effect shows the state reply
    // ahead of it has been handled.
    replyTrackingState(0);
    const QByteArray recognitionOn = SiyiProtocol::encodeRaw(
        static_cast<quint8>(SiyiAi::CommandId::RequestRecognitionState), QByteArray(1, '\1'));
    QCOMPARE(module.writeDatagram(recognitionOn, controllerAddress, controllerPort), recognitionOn.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.recognitionEnabled(), TestTimeout::shortMs());
    QVERIFY(!drainCommands(module).contains(static_cast<quint8>(SiyiAi::CommandId::SetTrackTarget)));

    // Still holding a target: the cancel goes out. The firmware also answers 2 while a lock is
    // being established, so anything non-zero has to count as something to cancel.
    controller.cancelTracking();
    QVERIFY(module.waitForReadyRead(TestTimeout::shortMs()));
    (void) drainCommands(module);
    replyTrackingState(2);

    // Accumulated, not re-read: draining consumes, and QTRY_VERIFY evaluates its condition more
    // than once. Same list as above, which is why it is only cleared and not redeclared.
    sent.clear();
    QTRY_VERIFY_WITH_TIMEOUT((sent += drainCommands(module))
                                 .contains(static_cast<quint8>(SiyiAi::CommandId::SetTrackTarget)),
                             TestTimeout::shortMs());
}

/// The ask before the cancel is bounded, not conditional. This link is UDP with nothing that
/// retransmits, so the query or its reply can simply vanish - and a cancel that is then never sent
/// leaves the module tracking and the gimbal following a person after the operator has been told
/// it stopped. The wrong way round of that trade is a phantom selection box, which the operator
/// can see and clear.
void SiyiAiControllerTest::_cancelGoesOutWhenTheModuleNeverAnswers_test()
{
    QUdpSocket module;
    QHostAddress controllerAddress;
    quint16 controllerPort = 0;

    SiyiAiController controller(nullptr);
    openFakeModule(module, controller, controllerAddress, controllerPort);
    (void) drainFrames(module);

    // A target on screen first, so there is something for the cancel to clear and something for
    // the dashboard to be wrong about.
    const QByteArray tracking = targetStreamFrame(SiyiAi::TrackingStatus::Tracking);
    QCOMPARE(module.writeDatagram(tracking, controllerAddress, controllerPort), tracking.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasTarget(), TestTimeout::shortMs());

    controller.cancelTracking();

    // The module is still tracking until a cancel actually reaches it, so the target may not
    // disappear from the UI yet. Clearing it here is how the dashboard ends up saying "cancelled"
    // over a gimbal that is still following.
    QVERIFY(controller.hasTarget());

    // Nothing ever answers the state query.
    QList<SiyiProtocol::Frame> sent;
    QTRY_VERIFY_WITH_TIMEOUT(cancelCount(sent += drainFrames(module)) == 1, TestTimeout::shortMs());
    QVERIFY(!controller.hasTarget());
}

/// A cancel whose reply is slow, and an operator who picks a new target while it is in flight. The
/// late reply reports the module tracking - it is, the target just picked - and cancelling on that
/// clears a selection the operator made after the cancel, which on screen looks like the selection
/// failing by itself.
void SiyiAiControllerTest::_lateTrackingStateDoesNotCancelANewTarget_test()
{
    QUdpSocket module;
    QHostAddress controllerAddress;
    quint16 controllerPort = 0;

    SiyiAiController controller(nullptr);
    openFakeModule(module, controller, controllerAddress, controllerPort);

    controller.cancelTracking();
    QVERIFY(module.waitForReadyRead(TestTimeout::shortMs()));
    (void) drainFrames(module);

    controller.trackBox(0.4, 0.4, 0.6, 0.6);

    const QByteArray late = SiyiProtocol::encodeRaw(
        static_cast<quint8>(SiyiAi::CommandId::RequestTrackingState), QByteArray(1, '\1'));
    QCOMPARE(module.writeDatagram(late, controllerAddress, controllerPort), late.size());

    // The poll's own 1 Hz status request is the clock here. Seeing one means more time has passed
    // than the unanswered-cancel timeout needs, so both routes a cancel could still take - the late
    // reply being acted on, and the timeout in _poll() - have had their chance and taken neither.
    QList<SiyiProtocol::Frame> sent;
    const auto sawStatusPoll = [&]() {
        sent += drainFrames(module);
        for (const SiyiProtocol::Frame &frame : sent) {
            if (static_cast<quint8>(frame.commandId) ==
                static_cast<quint8>(SiyiAi::CommandId::RequestRecognitionState)) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(sawStatusPoll(), TestTimeout::mediumMs());

    QCOMPARE(selectionCount(sent), 1);
    QCOMPARE(cancelCount(sent), 0);
}

/// The other half of the same problem, and the dangerous half: a cancel that is outstanding when
/// an older query's answer turns up.
///
/// A cancel is a two-frame exchange on a UDP link with no retransmission and no ordering, and the
/// two frames can disagree because the module drops its own selection flag whenever it loses a
/// target. So a query sent before the operator picked a new person is answered "holding nothing" -
/// true when it was sent, false by the time it lands. Consumed as the answer to the cancel that is
/// actually outstanding, it sends no 0x06, ends the cancel, and takes the target off the screen
/// while the module is still tracking; with 0xC3 follow on, the aircraft goes on chasing that
/// person after the operator was told it had stopped. The stop path disappears silently, which is
/// the one thing it may never do.
void SiyiAiControllerTest::_staleTrackingStateDoesNotConsumeALaterCancel_test()
{
    QUdpSocket module;
    QHostAddress controllerAddress;
    quint16 controllerPort = 0;

    SiyiAiController controller(nullptr);
    openFakeModule(module, controller, controllerAddress, controllerPort);
    (void) drainFrames(module);

    const QByteArray tracking = targetStreamFrame(SiyiAi::TrackingStatus::Tracking);
    QCOMPARE(module.writeDatagram(tracking, controllerAddress, controllerPort), tracking.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasTarget(), TestTimeout::shortMs());

    // Cancel once. Its answer is slow.
    controller.cancelTracking();
    QVERIFY(module.waitForReadyRead(TestTimeout::shortMs()));
    const int firstQuery = lastTrackingQuery(drainFrames(module));
    QVERIFY(firstQuery >= 0);

    // The operator picks somebody else instead, which supersedes that cancel by design.
    controller.trackBox(0.4, 0.4, 0.6, 0.6);
    QVERIFY(module.waitForReadyRead(TestTimeout::shortMs()));
    (void) drainFrames(module);

    // And then cancels again. This is the cancel that has to survive.
    controller.cancelTracking();
    QVERIFY(module.waitForReadyRead(TestTimeout::shortMs()));
    const int secondQuery = lastTrackingQuery(drainFrames(module));
    QVERIFY(secondQuery >= 0);
    QVERIFY(secondQuery != firstQuery);

    // The first query's answer arrives now: nothing was held when it was sent.
    controller._handleFrame(trackingStateFrame(0, static_cast<quint16>(firstQuery)));

    QVERIFY(controller._cancelPending);   // still owed, so the timeout in _poll() can still rescue it
    QVERIFY(controller.hasTarget());      // and the target the module is holding is still on screen

    // The answer to the query that is actually outstanding says the module is tracking, so the
    // cancel goes out.
    controller._handleFrame(trackingStateFrame(1, static_cast<quint16>(secondQuery)));
    QVERIFY(!controller._cancelPending);
    QVERIFY(!controller.hasTarget());
    QVERIFY(module.waitForReadyRead(TestTimeout::shortMs()));
    QCOMPARE(cancelCount(drainFrames(module)), 1);
}

/// Nobody has read a real module's class list, and the names in it belong to whichever model the
/// module has loaded. A model that calls people something else leaves nothing to sum, and the sum
/// of nothing is zero - which on a police dashboard reads as an empty street in the middle of a
/// crowd. Unknown has to look different from none.
void SiyiAiControllerTest::_unknownClassNamesReadAsUnknownNotZero_test()
{
    SiyiAiController controller(nullptr);

    expectLogMessage("SiyiCamera.SiyiAiController", QtWarningMsg,
                     QRegularExpression(QStringLiteral("no person class")));
    controller._handleCountFrame(classListFrame({QStringLiteral("pedestrian"), QStringLiteral("vehicle")}));
    verifyExpectedLogMessage();

    controller._handleCountFrame(countPushFrame({5, 2}));

    // The link is delivering numbers, they just cannot be attributed.
    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), -1);
    QCOMPARE(controller.vehicleCount(), -1);
}

/// Counts arrive as one tally per class in the module's own class order, which belongs to
/// whichever model it has loaded. The class list is deliberately not in the SDK's documented
/// order here: the mapping has to come from the names, never from the index.
void SiyiAiControllerTest::_moduleCountsReachProperties_test()
{
    SiyiAiController controller(nullptr);
    QSignalSpy countSpy(&controller, &SiyiAiController::countsChanged);

    QVERIFY(!controller.countsValid());

    // Tallies with no class list yet: positional numbers nobody can read.
    controller._handleCountFrame(countPushFrame({3, 1, 0, 0}));
    QVERIFY(!controller.countsValid());
    QCOMPARE(controller.personCount(), -1);

    controller._handleCountFrame(classListFrame({QStringLiteral("car"), QStringLiteral("person"),
                                                 QStringLiteral("bus"), QStringLiteral("truck")}));
    controller._handleCountFrame(countPushFrame({2, 3, 1, 4}));

    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 3);
    QCOMPARE(controller.vehicleCount(), 7);
    QVERIFY(!controller.personCountSaturated());
    QVERIFY(!controller.vehicleCountSaturated());
    QVERIFY(countSpy.count() >= 1);
}

/// A class tally is one unsigned byte, so 255 is a floor and not a total. Reporting it as a
/// number would show a crowd of three hundred as two hundred and fifty five, and the next one
/// along as forty four.
void SiyiAiControllerTest::_saturatedCountIsFlagged_test()
{
    SiyiAiController controller(nullptr);

    controller._handleCountFrame(classListFrame({QStringLiteral("person"), QStringLiteral("car")}));
    controller._handleCountFrame(countPushFrame({255, 12}));

    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 255);
    QVERIFY(controller.personCountSaturated());
    QCOMPARE(controller.vehicleCount(), 12);
    QVERIFY(!controller.vehicleCountSaturated());
}

/// This link has no version negotiation, so a firmware change can start sending anything at all.
/// Nothing malformed may crash, and nothing malformed may quietly replace good numbers with bad
/// ones - a wrong count on a police dashboard is worse than no count.
void SiyiAiControllerTest::_malformedCountFramesKeepLastGoodNumbers_test()
{
    SiyiAiController controller(nullptr);

    controller._handleCountFrame(classListFrame({QStringLiteral("person"), QStringLiteral("car")}));
    controller._handleCountFrame(countPushFrame({4, 1}));
    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 4);

    SiyiLongProtocol::Frame otherCommand = countPushFrame({9, 9});
    otherCommand.commandId = 0x74;                  // get_ip_config, same link
    controller._handleCountFrame(otherCommand);

    SiyiLongProtocol::Frame empty;
    empty.commandId = static_cast<quint8>(SiyiAi::PrivateCommandId::ObjectCount);
    controller._handleCountFrame(empty);

    SiyiLongProtocol::Frame noise;
    noise.commandId = static_cast<quint8>(SiyiAi::PrivateCommandId::ObjectCount);
    noise.data = QByteArray::fromHex("01ff20de");   // claims 32 classes, carries one tally
    controller._handleCountFrame(noise);

    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 4);
    QCOMPARE(controller.vehicleCount(), 1);

    // A different class count means every index just changed meaning. The numbers stop being
    // valid until the class list has been read again.
    controller._handleCountFrame(countPushFrame({1, 2, 3}));
    QVERIFY(!controller.countsValid());
}

/// The module answers the pre-cancel state query with a frame this build cannot read - the 0x90
/// stub is this firmware's own precedent for an update that leaves the dispatch entry in place and
/// empties the body. Treating that as "nothing to cancel" swallows the cancel twice over: no 0x06
/// goes out, and the timeout that exists to rescue an unanswered query has already been disarmed.
/// The operator is told tracking stopped while the module is still holding the target.
void SiyiAiControllerTest::_unreadableTrackingStateDoesNotSwallowTheCancel_test()
{
    QUdpSocket module;
    QHostAddress controllerAddress;
    quint16 controllerPort = 0;

    SiyiAiController controller(nullptr);
    openFakeModule(module, controller, controllerAddress, controllerPort);
    (void) drainFrames(module);

    const QByteArray tracking = targetStreamFrame(SiyiAi::TrackingStatus::Tracking);
    QCOMPARE(module.writeDatagram(tracking, controllerAddress, controllerPort), tracking.size());
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasTarget(), TestTimeout::shortMs());

    // Accumulated and matched by content, not compared exactly. start() sends two frames back to
    // back and the drain above only clears what the kernel had already queued, so the second one
    // lands in this drain often enough to make an exact compare a coin toss - measured at five
    // failures in ten runs. The claim being made here is narrower anyway: the query goes out, and
    // no cancel goes out with it.
    controller.cancelTracking();
    QList<quint8> sent;
    QTRY_VERIFY_WITH_TIMEOUT((sent += drainCommands(module))
                                 .contains(static_cast<quint8>(SiyiAi::CommandId::RequestTrackingState)),
                             TestTimeout::shortMs());
    QVERIFY(!sent.contains(static_cast<quint8>(SiyiAi::CommandId::SetTrackTarget)));

    // Well-formed frame, empty payload: the ack shape a firmware update leaves behind when it
    // guts a handler but keeps its table entry.
    const QByteArray unreadable = SiyiProtocol::encodeRaw(
        static_cast<quint8>(SiyiAi::CommandId::RequestTrackingState), QByteArray());
    QCOMPARE(module.writeDatagram(unreadable, controllerAddress, controllerPort), unreadable.size());

    // The timeout has to still be armed, so the cancel goes out anyway.
    QList<SiyiProtocol::Frame> frames;
    QTRY_VERIFY_WITH_TIMEOUT(cancelCount(frames += drainFrames(module)) == 1, TestTimeout::shortMs());
    QVERIFY(!controller.hasTarget());
}

/// Counting is switched on by writing {0x01}, and the module acks that write with the class count
/// followed by that many zero bytes - byte for byte a push saying the scene is empty. Applied as a
/// count it puts a confident 0 on a police dashboard before a single inference has run.
void SiyiAiControllerTest::_countingOnAckIsNotACountOfZero_test()
{
    SiyiAiController controller(nullptr);
    QTcpServer server;
    QTcpSocket *module = nullptr;
    QTcpSocket *const link = openFakeCountLink(controller, server, module);
    QVERIFY(link && module);
    controller._countSocket = link;

    controller._handleCountFrame(classListFrame({QStringLiteral("person"), QStringLiteral("car")}));
    (void) drainCountFrames(link, module);

    // Counting is off, so the controller owes a {0x01} to switch it on - on the next keep-alive
    // tick, not on the report itself; see _countingOffDoesNotLoopTheLink_test().
    controller._handleCountFrame(countingOffFrame());
    for (int tick = 0; tick < kKeepAliveWindowTicks; ++tick) controller._poll();
    const QList<SiyiLongProtocol::Frame> written = objectCountWrites(link, module);
    QCOMPARE(written.size(), 1);
    QCOMPARE(written.at(0).data, QByteArray(1, static_cast<char>(SiyiAi::ObjectCountMode::Start)));

    // The ack to that write. Nothing has been seen yet, so nothing may be claimed.
    controller._handleCountFrame(countPushFrame({0, 0}));
    QVERIFY(!controller.countsValid());
    QCOMPARE(controller.personCount(), -1);

    // The pushes behind it are real, zeroes included: only the ack is discounted, not every zero.
    controller._handleCountFrame(countPushFrame({3, 1}));
    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 3);

    controller._handleCountFrame(countPushFrame({0, 0}));
    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 0);
}

/// The link carries no version, so a firmware whose push and class list disagree can disagree
/// permanently. Re-reading the list from the mismatch itself makes that a request/reply loop
/// clocked by LAN round-trip time - milliseconds - for the rest of the flight. The card reading
/// "-" is the right answer; hammering the module is not.
void SiyiAiControllerTest::_classCountMismatchDoesNotLoopTheLink_test()
{
    SiyiAiController controller(nullptr);
    QTcpServer server;
    QTcpSocket *module = nullptr;
    QTcpSocket *const link = openFakeCountLink(controller, server, module);
    QVERIFY(link && module);
    controller._countSocket = link;

    controller._handleCountFrame(classListFrame({QStringLiteral("person"), QStringLiteral("car")}));
    controller._handleCountFrame(countPushFrame({4, 1}));
    QVERIFY(controller.countsValid());
    (void) drainCountFrames(link, module);

    // Three tallies against two names, over and over, as a module that renumbered its model but
    // kept answering the old class list would send them.
    for (int push = 0; push < 5; ++push) {
        controller._handleCountFrame(countPushFrame({1, 2, 3}));
    }

    // countsValid going false is what makes the card read "-"; the last good numbers are left
    // where they are, exactly as the malformed-frame case leaves them.
    QVERIFY(!controller.countsValid());
    QCOMPARE(drainCountFrames(link, module).size(), 0);

    // Positive control: the link really was live, so the zero above is a silence and not a rig
    // that could never have seen a frame in the first place.
    controller._sendCount(SiyiAi::PrivateCommandId::ObjectCount,
                          QByteArray(1, static_cast<char>(SiyiAi::ObjectCountMode::ClassList)));
    QCOMPARE(drainCountFrames(link, module).size(), 1);
}

/// Byte 1 of every 0xD5 reply and push is the model the module currently has loaded (spec section
/// 4), and the tallies after it are positional in that model's class order. The only guard
/// _applyCounts() has is the class count, so a swap to a model with the same number of classes
/// walks straight through it and index 0 goes on being summed as "person" after it stopped meaning
/// one. Nobody here initiates that swap or is told about it: the hand controller's UniGCS shares
/// this link - the same fact kCountReconnectInterval is written on - and can load a model over
/// 0xA9/0xAE at any time. A police dashboard would report vehicles as people, confidently, with
/// nothing on screen to say so.
void SiyiAiControllerTest::_modelSwapWithTheSameClassCountDropsTheMapping_test()
{
    SiyiAiController controller(nullptr);
    QTcpServer server;
    QTcpSocket *module = nullptr;
    QTcpSocket *const link = openFakeCountLink(controller, server, module);
    QVERIFY(link && module);
    controller._countSocket = link;

    controller._handleCountFrame(classListFrame({QStringLiteral("person"), QStringLiteral("car")}, 5));
    controller._handleCountFrame(countPushFrame({4, 1}, 5));
    QCOMPARE(controller.personCount(), 4);
    QCOMPARE(controller.vehicleCount(), 1);
    (void) drainCountFrames(link, module);

    // Same two-class shape, different model. Under the class count alone this is indistinguishable
    // from another push of the model that is mapped.
    controller._handleCountFrame(countPushFrame({9, 0}, 6));

    // Not nine people.
    QVERIFY(!controller.countsValid());
    QVERIFY(controller._classNames.isEmpty());

    // Re-read on the keep-alive tick and not on the push, so a module whose pushes and class list
    // permanently disagree cannot turn this into a request/reply loop at LAN round-trip time -
    // the same floor the class-count mismatch relies on.
    QCOMPARE(objectCountWrites(link, module).size(), 0);
    for (int tick = 0; tick < kKeepAliveWindowTicks; ++tick) controller._poll();
    const QList<SiyiLongProtocol::Frame> writes = objectCountWrites(link, module);
    QVERIFY(!writes.isEmpty());
    QCOMPARE(writes.at(0).data, QByteArray(1, static_cast<char>(SiyiAi::ObjectCountMode::ClassList)));

    // The new model's list arrives, and the same row now reads as what it actually is.
    controller._handleCountFrame(classListFrame({QStringLiteral("car"), QStringLiteral("person")}, 6));
    controller._handleCountFrame(countPushFrame({9, 0}, 6));
    QCOMPARE(controller.personCount(), 0);
    QCOMPARE(controller.vehicleCount(), 9);
}

/// The mirror of _classCountMismatchDoesNotLoopTheLink_test() on the sibling branch. A module that
/// will not switch counting on answers a Start with another "counting is off" - the link carries no
/// version negotiation, so a firmware that renumbers the modes or empties the handler the way v1.1.0
/// emptied 0x90 does exactly that. Writing the Start from the report itself made that a 21-byte
/// frame per LAN round trip, a few milliseconds apart, for the whole flight.
void SiyiAiControllerTest::_countingOffDoesNotLoopTheLink_test()
{
    SiyiAiController controller(nullptr);
    QTcpServer server;
    QTcpSocket *module = nullptr;
    QTcpSocket *const link = openFakeCountLink(controller, server, module);
    QVERIFY(link && module);
    controller._countSocket = link;

    controller._handleCountFrame(classListFrame({QStringLiteral("person"), QStringLiteral("car")}));
    (void) drainCountFrames(link, module);

    // A module that refuses, answering every Start with mode 0 again.
    for (int report = 0; report < 20; ++report) {
        controller._handleCountFrame(countingOffFrame());
        QCOMPARE(drainCountFrames(link, module).size(), 0);
    }

    // One window, one Start. Not twenty.
    for (int tick = 0; tick < kKeepAliveWindowTicks; ++tick) controller._poll();
    const QList<SiyiLongProtocol::Frame> firstWindow = objectCountWrites(link, module);
    QCOMPARE(firstWindow.size(), 1);
    QCOMPARE(firstWindow.at(0).data, QByteArray(1, static_cast<char>(SiyiAi::ObjectCountMode::Start)));

    // And no further Start until the module says "off" again. What the five windows behind it do
    // carry is one bare state query each - the counts are not valid, so the keep-alive branch is
    // asking whether counting came back, which is the sibling fix and rationed the same way.
    for (int tick = 0; tick < (5 * kKeepAliveWindowTicks); ++tick) controller._poll();
    const QList<SiyiLongProtocol::Frame> idle = objectCountWrites(link, module);
    QCOMPARE(idle.size(), 5);
    for (const SiyiLongProtocol::Frame &frame : idle) {
        QCOMPARE(frame.data, QByteArray());
    }

    // One more refusal buys exactly one more Start, whatever the module says in between.
    for (int report = 0; report < 10; ++report) {
        controller._handleCountFrame(countingOffFrame());
    }
    for (int tick = 0; tick < kKeepAliveWindowTicks; ++tick) controller._poll();
    const QList<SiyiLongProtocol::Frame> secondWindow = objectCountWrites(link, module);
    QCOMPARE(secondWindow.size(), 1);
    QCOMPARE(secondWindow.at(0).data, QByteArray(1, static_cast<char>(SiyiAi::ObjectCountMode::Start)));
}

/// Counting can die under a link that stays perfectly healthy: the hand controller's UniGCS
/// switches "AI recognition push" off, or the module reloads its model, and the module clears its
/// own flag. The pushes stop, the TCP link lives on its keep-alives, and nothing reconnects. Before
/// this the state query only ever went out when the link opened, so the delivery's headline number
/// stayed dead for the rest of the flight.
void SiyiAiControllerTest::_countingIsRestartedWhenTheModuleDropsItsFlag_test()
{
    SiyiAiController controller(nullptr);
    QTcpServer server;
    QTcpSocket *module = nullptr;
    QTcpSocket *const link = openFakeCountLink(controller, server, module);
    QVERIFY(link && module);
    controller._countSocket = link;

    controller._handleCountFrame(classListFrame({QStringLiteral("person"), QStringLiteral("car")}));
    controller._handleCountFrame(countPushFrame({4, 1}));
    QVERIFY(controller.countsValid());

    // While the numbers are flowing the keep-alive tick asks for nothing: the module is already
    // saying everything there is to say.
    for (int tick = 0; tick < (3 * kKeepAliveWindowTicks); ++tick) controller._poll();
    QCOMPARE(objectCountWrites(link, module).size(), 0);

    // The pushes stop. _poll() lets the counts go stale exactly as kCountTimeoutMs does.
    controller._setCountsValid(false);

    for (int tick = 0; tick < kKeepAliveWindowTicks; ++tick) controller._poll();
    const QList<SiyiLongProtocol::Frame> query = objectCountWrites(link, module);
    QCOMPARE(query.size(), 1);
    QCOMPARE(query.at(0).data, QByteArray());        // bare state query

    // The module answers "off", which arms the Start on the next window, and counting comes back.
    controller._handleCountFrame(countingOffFrame());
    for (int tick = 0; tick < kKeepAliveWindowTicks; ++tick) controller._poll();
    const QList<SiyiLongProtocol::Frame> start = objectCountWrites(link, module);
    QCOMPARE(start.size(), 1);
    QCOMPARE(start.at(0).data, QByteArray(1, static_cast<char>(SiyiAi::ObjectCountMode::Start)));

    controller._handleCountFrame(countPushFrame({0, 0}));   // the Start ack, discounted
    controller._handleCountFrame(countPushFrame({7, 2}));
    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 7);
}

/// The module's private server takes many clients at once, and the hand controller is one of them.
/// If it switches counting on in the same instant we do, the first real push can overtake our Start
/// ack. Discounting "the next frame to arrive" would then throw that push away and apply the ack -
/// a row of zeroes - as a count, printing "0 people" with countsValid true from the middle of a
/// crowd. The zero row is what identifies the ack; arrival order does not.
void SiyiAiControllerTest::_startAckIsToldApartByItsZeroRowNotItsOrder_test()
{
    SiyiAiController controller(nullptr);
    QTcpServer server;
    QTcpSocket *module = nullptr;
    QTcpSocket *const link = openFakeCountLink(controller, server, module);
    QVERIFY(link && module);
    controller._countSocket = link;

    controller._handleCountFrame(classListFrame({QStringLiteral("person"), QStringLiteral("car")}));
    controller._handleCountFrame(countingOffFrame());
    for (int tick = 0; tick < kKeepAliveWindowTicks; ++tick) controller._poll();
    QCOMPARE(objectCountWrites(link, module).size(), 1);   // the Start

    // Somebody else's push, carrying real people, lands before our ack does.
    controller._handleCountFrame(countPushFrame({12, 3}));
    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 12);

    // Our ack, behind it. It must not overwrite twelve people with zero.
    controller._handleCountFrame(countPushFrame({0, 0}));
    QCOMPARE(controller.personCount(), 12);

    // And the discount is spent: the scene really emptying is reported.
    controller._handleCountFrame(countPushFrame({0, 0}));
    QVERIFY(controller.countsValid());
    QCOMPARE(controller.personCount(), 0);
}

/// The module refuses to run recognition on video above 1920x1080 and says so only in the second
/// byte of the reply to our own write (apt_set_ai_switch@0x577c80 returns 0x0600). Four things have
/// to hold at once or the card reads as a dead link instead of a refusal the operator can fix: the
/// refusal is taken from the write's reply, the plain state reply's short payload does not clear
/// it, recognition actually running does clear it, and it does not outlive the session it belongs
/// to.
void SiyiAiControllerTest::_streamTooLargeSurvivesTheStateReply_test()
{
    SiyiAiController controller(nullptr);
    QSignalSpy tooLargeSpy(&controller, &SiyiAiController::streamTooLargeChanged);

    SiyiProtocol::Frame refusal;
    refusal.commandId = static_cast<SiyiProtocol::CommandId>(SiyiAi::CommandId::SetRecognitionState);
    refusal.data = QByteArray::fromHex("0006");
    expectLogMessage("SiyiCamera.SiyiAiController", QtWarningMsg,
                     QRegularExpression(QStringLiteral("above 1920x1080")));
    controller._handleFrame(refusal);
    verifyExpectedLogMessage();
    QVERIFY(controller.streamTooLarge());
    QCOMPARE(tooLargeSpy.count(), 1);

    // The 1 Hz state request answers a single byte. Reading a refusal code out of that one would
    // clear the flag a second after the refusal set it.
    SiyiProtocol::Frame stateOff;
    stateOff.commandId = static_cast<SiyiProtocol::CommandId>(SiyiAi::CommandId::RequestRecognitionState);
    stateOff.data = QByteArray(1, '\0');
    controller._handleFrame(stateOff);
    QVERIFY(controller.streamTooLarge());
    QCOMPARE(tooLargeSpy.count(), 1);

    // Recognition running means the refusal has been overtaken by events - the resolution was
    // lowered, or the hand controller's app started it.
    SiyiProtocol::Frame stateOn;
    stateOn.commandId = static_cast<SiyiProtocol::CommandId>(SiyiAi::CommandId::RequestRecognitionState);
    stateOn.data = QByteArray(1, '\1');
    controller._handleFrame(stateOn);
    QVERIFY(!controller.streamTooLarge());
    QCOMPARE(tooLargeSpy.count(), 2);

    // And a refusal does not survive the link it was given on: a module that reboots would
    // otherwise wear "video too large" for the rest of the flight while it is working.
    expectLogMessage("SiyiCamera.SiyiAiController", QtWarningMsg,
                     QRegularExpression(QStringLiteral("above 1920x1080")));
    controller._handleFrame(refusal);
    verifyExpectedLogMessage();
    QVERIFY(controller.streamTooLarge());
    controller._setConnected(true);
    controller._setConnected(false);
    QVERIFY(!controller.streamTooLarge());
}

UT_REGISTER_TEST(SiyiAiControllerTest, TestLabel::Unit)
