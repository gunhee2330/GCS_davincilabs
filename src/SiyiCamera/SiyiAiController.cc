#include "SiyiAiController.h"

#include <algorithm>

#include <QtCore/QApplicationStatic>
#include <QtCore/QVariant>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QJSEngine>

#include "Fact.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "SiyiCameraSettings.h"
#include "SiyiLongProtocol.h"
#include "SiyiProtocol.h"

QGC_LOGGING_CATEGORY(SiyiAiControllerLog, "SiyiCamera.SiyiAiController")

namespace {

constexpr int kPollIntervalMs = 200;

/// The module is declared offline once this long passes with no valid frame.
constexpr qint64 kConnectionTimeoutMs = 2000;

/// A target vanishes from the UI when its stream stalls this long.
constexpr qint64 kTargetTimeoutMs = 1500;

/// Poll tick count for the 1 Hz status refresh, in units of kPollIntervalMs.
constexpr int kStatusInterval = 5;

/// Counts are stale this long after the last push. The module pushes them per inference frame,
/// so anything near a second of silence means the numbers have stopped, not slowed.
constexpr qint64 kCountTimeoutMs = 2000;

/// Poll ticks between attempts on the count link. Plain reconnect backoff, nothing more: with no
/// module on the network this runs for the whole flight, and abort()+connect() every three
/// seconds is cheap while a tighter loop is not.
///
/// It is NOT contention avoidance. The module's private server accepts many clients at once -
/// set_listen@0x55c7e4 passes a backlog of 128 (mov w1,#0x80) to listen(), tcp_server_init@0x55c31c
/// multiplexes the accepted descriptors through epoll_create/epoll_ctl across three worker threads,
/// and per-connection state is an array indexed by the connection byte (0x56d134). So UniGCS or the
/// hand controller holding the link does not lock us out, and we do not evict them: both can read
/// the counts at the same time.
constexpr int kCountReconnectInterval = 15;

/// Poll ticks between keep-alives, i.e. four seconds. The deadline this stays inside was not
/// measured: tcp_server_init@0x55c31c carries what looks like a -10 second idle field, and CMD
/// 0x80 is assumed to push it back, but neither was disassembled and the spec still lists the idle
/// disconnect among the open bench questions. Four seconds is chosen to sit well inside the
/// shortest plausible reading rather than from a known timeout; the traffic is one empty frame, so
/// being early costs nothing and being late costs the count link.
constexpr int kCountKeepAliveInterval = 20;

/// How long cancelTracking() waits for the module to say whether it still holds a selection
/// before cancelling anyway. This link is UDP with no retransmission of any kind, so either the
/// query or its reply can simply vanish; the gate exists to stop a cancel from arming a phantom
/// selection when nothing is selected, not to abandon the cancel when the module goes quiet. A
/// wrong cancel costs a phantom box the operator can clear. A cancel that never goes out leaves
/// the gimbal following a person after the operator was told it had stopped.
constexpr qint64 kCancelReplyTimeoutMs = 300;

/// Second byte of a SetRecognitionState reply when the module declines because its input video
/// is above 1920x1080 (apt_set_ai_switch@0x577c80 returns 0x0600, little endian). Undocumented,
/// and the only clue an operator gets that recognition is refusing rather than absent.
constexpr quint8 kStreamTooLargeCode = 0x06;

quint16 toReference(double normalised, int span)
{
    return static_cast<quint16>(qRound(qBound(0.0, normalised, 1.0) * (span - 1)));
}

// The module numbers its own classes and renumbers them whenever its model changes, so the two
// the operator is shown are matched by name every time the class list arrives, never by index.
// A class named anything else counts as neither: an unrecognised name is not a person.
bool isPersonClass(const QString &name)
{
    return name.compare(QLatin1StringView("person"), Qt::CaseInsensitive) == 0;
}

bool isVehicleClass(const QString &name)
{
    for (const QLatin1StringView vehicle : {QLatin1StringView("car"), QLatin1StringView("bus"),
                                            QLatin1StringView("truck")}) {
        if (name.compare(vehicle, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool isFireClass(const QString &name)
{
    return name.compare(QLatin1StringView("fire"), Qt::CaseInsensitive) == 0;
}

bool isSmokeClass(const QString &name)
{
    return name.compare(QLatin1StringView("smoke"), Qt::CaseInsensitive) == 0;
}

bool isBoatClass(const QString &name)
{
    return name.compare(QLatin1StringView("boat"), Qt::CaseInsensitive) == 0;
}

QByteArray objectCountPayload(SiyiAi::ObjectCountMode mode)
{
    return QByteArray(1, static_cast<char>(mode));
}

} // namespace

Q_APPLICATION_STATIC(SiyiAiController, _siyiAiControllerInstance, nullptr);

SiyiAiController::SiyiAiController(QObject *parent)
    : QObject(parent)
{
    _pollTimer.setInterval(kPollIntervalMs);
    (void) connect(&_pollTimer, &QTimer::timeout, this, &SiyiAiController::_poll);
}

SiyiAiController::~SiyiAiController()
{
    stop();
}

SiyiAiController *SiyiAiController::instance()
{
    return _siyiAiControllerInstance();
}

SiyiAiController *SiyiAiController::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    SiyiAiController *const controller = instance();
    QJSEngine::setObjectOwnership(controller, QJSEngine::CppOwnership);
    return controller;
}

void SiyiAiController::init()
{
    SiyiCameraSettings *const settings = SettingsManager::instance()->siyiCameraSettings();
    if (!settings) {
        qCWarning(SiyiAiControllerLog) << "settings unavailable";
        return;
    }

    const auto applySettings = [this, settings]() {
        if (settings->aiEnabled()->rawValue().toBool()) {
            start();
        } else {
            stop();
        }
    };

    for (Fact *const fact : {settings->aiEnabled(), settings->aiIpAddress(), settings->aiPort()}) {
        (void) connect(fact, &Fact::rawValueChanged, this, [applySettings](const QVariant &) { applySettings(); });
    }

    applySettings();
}

void SiyiAiController::start()
{
    stop();

    SiyiCameraSettings *const settings = SettingsManager::instance()->siyiCameraSettings();
    if (!settings) {
        qCWarning(SiyiAiControllerLog) << "settings unavailable";
        return;
    }

    const QString address = settings->aiIpAddress()->rawValue().toString();
    if (!_moduleAddress.setAddress(address)) {
        qCWarning(SiyiAiControllerLog) << "invalid module address:" << address;
        return;
    }
    _modulePort = static_cast<quint16>(settings->aiPort()->rawValue().toUInt());

    _socket = new QUdpSocket(this);
    if (!_socket->bind(QHostAddress::AnyIPv4, 0)) {
        qCWarning(SiyiAiControllerLog) << "bind failed:" << _socket->errorString();
        delete _socket;
        _socket = nullptr;
        return;
    }
    (void) connect(_socket, &QUdpSocket::readyRead, this, &SiyiAiController::_readPendingDatagrams);

    // Second link, to the same module's undocumented TCP port, purely for object counts. It is
    // brought up and retried from _poll() so a module that is absent, busy with the hand
    // controller's app, or simply older than this feature costs nothing but a socket.
    _countSocket = new QTcpSocket(this);
    (void) connect(_countSocket, &QTcpSocket::readyRead, this, &SiyiAiController::_readCountLink);
    (void) connect(_countSocket, &QTcpSocket::connected, this, &SiyiAiController::_countLinkOpened);
    (void) connect(_countSocket, &QTcpSocket::disconnected, this, [this]() {
        qCDebug(SiyiAiControllerLog) << "count link closed";
        _countRxBuffer.clear();
        _classNames.clear();
        _personClasses.clear();
        _vehicleClasses.clear();
        _setCountsValid(false);
    });

    qCDebug(SiyiAiControllerLog) << "connecting to" << _moduleAddress << _modulePort;

    _rxBuffer.clear();
    _countRxBuffer.clear();
    _pollTicks = 0;
    _streamRequested = false;
    _cancelPending = false;
    _lastFrameTimer.start();
    _lastTargetTimer.invalidate();
    _lastCountTimer.invalidate();
    _pollTimer.start();

    _countSocket->connectToHost(_moduleAddress, SiyiAi::kPrivatePort);

    _send(SiyiAi::encodeRequest(SiyiAi::CommandId::RequestFirmwareVersion, _sequence++));
    _send(SiyiAi::encodeRequest(SiyiAi::CommandId::RequestRecognitionState, _sequence++));
}

void SiyiAiController::stop()
{
    _pollTimer.stop();

    if (_socket) {
        _socket->deleteLater();
        _socket = nullptr;
    }
    if (_countSocket) {
        _countSocket->abort();
        _countSocket->deleteLater();
        _countSocket = nullptr;
    }

    _rxBuffer.clear();
    _countRxBuffer.clear();
    _countStartAckPending = false;
    _countStartWanted = false;
    _classNames.clear();
    _personClasses.clear();
    _vehicleClasses.clear();
    _streamRequested = false;
    _cancelPending = false;
    _setHasTarget(false);
    _setConnected(false);
    _setCountsValid(false);
    _setStreamTooLarge(false);
}

void SiyiAiController::setRecognition(bool enabled)
{
    _send(SiyiAi::encodeSetRecognition(enabled, _sequence++));
    _send(SiyiAi::encodeRequest(SiyiAi::CommandId::RequestRecognitionState, _sequence++));
}

void SiyiAiController::setStreamWidth(int width)
{
    if ((width <= 0) || (width == _streamWidth)) {
        return;
    }
    _streamWidth = width;
    emit streamResolutionChanged();
}

void SiyiAiController::setStreamHeight(int height)
{
    if ((height <= 0) || (height == _streamHeight)) {
        return;
    }
    _streamHeight = height;
    emit streamResolutionChanged();
}

void SiyiAiController::trackPoint(double x, double y)
{
    // A new selection supersedes a cancel still waiting on its state reply. Without this the late
    // reply sees the module tracking - it is, the new target - and cancels the target the operator
    // just picked, which looks like the selection failing for no reason on screen.
    _cancelPending = false;

    // Selection coordinates are in the video stream's own resolution, not the reference
    // frame the target stream reports in.
    _send(SiyiAi::encodeTrackPoint(toReference(x, _streamWidth),
                                   toReference(y, _streamHeight),
                                   _sequence++));
}

void SiyiAiController::trackBox(double left, double top, double right, double bottom)
{
    _cancelPending = false;     // See trackPoint().
    _send(SiyiAi::encodeTrackBox(toReference(left, _streamWidth),
                                 toReference(top, _streamHeight),
                                 toReference(right, _streamWidth),
                                 toReference(bottom, _streamHeight),
                                 _sequence++));
}

void SiyiAiController::cancelTracking()
{
    // Ask before cancelling. A cancel that arrives when the module holds no selection does not
    // clear anything: apt_select_ai_target@0x577930 reads its selection flag, finds it already
    // down, and falls into the tap-selection branch (rx == 0 || ry == 0 at 0x577af4), which arms
    // a fresh selection instead. The module drops that flag by itself the moment it loses a
    // target, so the operator pressing cancel just after an automatic loss is exactly the case
    // that arms a phantom.
    //
    // The private link has a command named cancel_ai_target_tracking (0xAC) that looks like the
    // fix and is not. Its handler is 104 bytes -
    // server_0_camera_sdk_cancel_ai_target_tracking_action@0x56d6c0 - and all it does is call
    // apt_ai_is_track@0x578d10, invert the answer (tst w0,#0xff / cset w4,eq) and put that byte in
    // the ack. There is no store to module state anywhere in its call graph: it reads the tracking
    // flag and never writes it. This documented 0x05 query returns the same flag on the link
    // already in use, so nothing new is needed.
    //
    // The ask is bounded, not conditional: _poll() sends the cancel unasked once the reply is
    // overdue. And the target stays on screen until one of the two actually happens, because the
    // module is still tracking until then and a dashboard that says otherwise is lying about
    // where the gimbal is pointing.
    _cancelPending = true;
    _cancelSequence = _sequence;
    _cancelTimer.start();
    _send(SiyiAi::encodeRequest(SiyiAi::CommandId::RequestTrackingState, _sequence++));
}

void SiyiAiController::_send(const QByteArray &packet)
{
    if (!_socket) {
        return;
    }
    if (_socket->writeDatagram(packet, _moduleAddress, _modulePort) < 0) {
        qCWarning(SiyiAiControllerLog) << "send failed:" << _socket->errorString();
    }
}

void SiyiAiController::_readPendingDatagrams()
{
    if (!_socket) {
        return;
    }

    while (_socket->hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(_socket->pendingDatagramSize()), Qt::Uninitialized);
        const qint64 read = _socket->readDatagram(datagram.data(), datagram.size());
        if (read < 0) {
            qCWarning(SiyiAiControllerLog) << "read failed:" << _socket->errorString();
            return;
        }
        datagram.truncate(static_cast<int>(read));
        _rxBuffer.append(datagram);
    }

    const QList<SiyiProtocol::Frame> frames = SiyiProtocol::decode(_rxBuffer);
    for (const SiyiProtocol::Frame &frame : frames) {
        _lastFrameTimer.restart();
        _setConnected(true);
        _handleFrame(frame);
    }
}

void SiyiAiController::_handleFrame(const SiyiProtocol::Frame &frame)
{
    switch (static_cast<SiyiAi::CommandId>(frame.commandId)) {
    case SiyiAi::CommandId::SetRecognitionState:
        // Second byte only on the reply to our own write. The plain state request answers a
        // single byte, so reading a refusal code out of that one would clear the flag a second
        // after the refusal set it.
        _setStreamTooLarge((frame.data.size() >= 2) &&
                           (static_cast<quint8>(frame.data.at(1)) == kStreamTooLargeCode));
        [[fallthrough]];
    case SiyiAi::CommandId::RequestRecognitionState: {
        const auto enabled = SiyiAi::parseEnabledFlag(frame.data);
        if (enabled && *enabled) {
            // Recognition is running, so an earlier size refusal has been overtaken by events -
            // the resolution was lowered, or the hand controller's app turned it on. Note the
            // fallthrough above cannot land here on a refusal: 0x0600 reports enabled = 0.
            _setStreamTooLarge(false);
        }
        if (enabled && (*enabled != _recognitionEnabled)) {
            _recognitionEnabled = *enabled;
            emit recognitionEnabledChanged();
        }
        break;
    }

    case SiyiAi::CommandId::SetTrackTarget: {
        const auto result = SiyiAi::parseTrackRequestResult(frame.data);
        if (!result) {
            break;
        }
        switch (*result) {
        case SiyiAi::TrackRequestResult::Accepted:
            break;
        case SiyiAi::TrackRequestResult::Error:
            emit trackRequestFailed(tr("AI module rejected the target"));
            break;
        case SiyiAi::TrackRequestResult::NotInTrackingMode:
            emit trackRequestFailed(tr("AI tracking mode is not active"));
            break;
        case SiyiAi::TrackRequestResult::StreamUnsupported:
            emit trackRequestFailed(tr("Current video stream does not support AI tracking"));
            break;
        }
        break;
    }

    case SiyiAi::CommandId::RequestTrackingState: {
        if (!_cancelPending) {
            break;
        }
        if (frame.sequence != _cancelSequence) {
            // Some other query's answer, arriving late or twice - UDP, no retransmission and no
            // ordering. Read as this cancel's answer it is worse than useless: the module drops
            // its own selection flag whenever it loses a target, so the reply to a query sent
            // before a new target was picked says "holding nothing", and consuming it here sends
            // no 0x06, clears the pending cancel and takes the target off the screen while the
            // module is still tracking the new one - with follow on, the aircraft goes on chasing
            // it. Left pending instead, so kCancelReplyTimeoutMs still rescues the cancel.
            //
            // Whether this module echoes a request's sequence at all was never established (the
            // same doubt is written down in _handleCountFrame(), and the spec leaves it among the
            // open bench questions). A module that does not echo sends every cancel through that
            // timeout and its unconditional 0x06 - which is the trade kCancelReplyTimeoutMs is
            // already written for: a wrong cancel costs a phantom box the operator can clear, a
            // lost one leaves the gimbal following a person after the operator was told it stopped.
            qCDebug(SiyiAiControllerLog) << "tracking state for sequence" << frame.sequence
                                         << "is not the answer to cancel" << _cancelSequence;
            break;
        }
        // The manual documents 0 and 1 here; the firmware also answers 2 while a lock is being
        // established, so this is a "is there anything to cancel" test, not a boolean.
        const auto tracking = SiyiAi::parseEnabledFlag(frame.data);
        if (!tracking) {
            // A well-formed frame this cannot read - the 0x90 stub is this firmware's precedent
            // for an update leaving the dispatch entry and emptying the body - so the answer is
            // "unknown", not "nothing to cancel". Consuming _cancelPending here would drop the
            // cancel and also disarm the timeout in _poll() that exists to rescue it, and the
            // operator would be told tracking stopped while the module still holds the target.
            qCDebug(SiyiAiControllerLog) << "tracking state unreadable, leaving the cancel to time out";
            break;
        }
        _cancelPending = false;
        if (*tracking) {
            _send(SiyiAi::encodeCancelTracking(_sequence++));
        }
        // Either the cancel has gone out or the module says it is holding nothing. Only now may
        // the target leave the screen.
        _setHasTarget(false);
        break;
    }

    case SiyiAi::CommandId::RequestTargetStreamState: {
        const auto state = SiyiAi::parseTargetStreamState(frame.data);
        // Re-open the coordinate stream whenever the module reports it closed.
        if (state && (*state == SiyiAi::TargetStreamState::Closed)) {
            _send(SiyiAi::encodeSetTargetStream(true, _sequence++));
        }
        break;
    }

    case SiyiAi::CommandId::TargetStream: {
        const auto target = SiyiAi::parseTargetStream(frame.data);
        if (target) {
            _target = *target;
            _lastTargetTimer.restart();
            // A cancel from the hand controller or SIYI's own app is only reported here, so
            // drop the target now instead of waiting out kTargetTimeoutMs.
            _hasTarget = (_target.status != SiyiAi::TrackingStatus::CancelledByUser);
            emit targetChanged();
        }
        break;
    }

    default:
        qCDebug(SiyiAiControllerLog) << "unhandled command:" << static_cast<int>(frame.commandId);
        break;
    }
}

void SiyiAiController::_poll()
{
    ++_pollTicks;

    if (_connected && (_lastFrameTimer.elapsed() > kConnectionTimeoutMs)) {
        _setConnected(false);
    }
    if (_hasTarget && _lastTargetTimer.isValid() && _lastTargetTimer.hasExpired(kTargetTimeoutMs)) {
        _setHasTarget(false);
    }
    if (_countsValid && _lastCountTimer.isValid() && _lastCountTimer.hasExpired(kCountTimeoutMs)) {
        _setCountsValid(false);
    }
    if (_cancelPending && _cancelTimer.hasExpired(kCancelReplyTimeoutMs)) {
        // No answer to the state query, so send the cancel without one; see kCancelReplyTimeoutMs.
        _cancelPending = false;
        _send(SiyiAi::encodeCancelTracking(_sequence++));
        _setHasTarget(false);
    }

    if (_countSocket) {
        if (_countSocket->state() != QAbstractSocket::ConnectedState) {
            if ((_pollTicks % kCountReconnectInterval) == 0) {
                // abort() first so an attempt that got no answer cannot sit in SYN retries for
                // half a minute; this doubles as the connect timeout. Deliberately quiet: with
                // no module on the network this runs for the whole flight.
                _countSocket->abort();
                _countSocket->connectToHost(_moduleAddress, SiyiAi::kPrivatePort);
            }
        } else if ((_pollTicks % kCountKeepAliveInterval) == 0) {
            _sendCount(SiyiAi::PrivateCommandId::KeepAlive);
            if (_classNames.isEmpty()) {
                // Without the class list the tallies are unreadable, and it is otherwise only
                // asked for once per connection - a single lost reply would cost the whole
                // flight's counts.
                _sendCount(SiyiAi::PrivateCommandId::ObjectCount,
                           objectCountPayload(SiyiAi::ObjectCountMode::ClassList));
            }
            if (_countStartWanted) {
                _countStartWanted = false;
                _countStartAckPending = true;
                _sendCount(SiyiAi::PrivateCommandId::ObjectCount,
                           objectCountPayload(SiyiAi::ObjectCountMode::Start));
            } else if (!_countsValid) {
                // The module's counting flag is its own and it drops it without telling us: the
                // hand controller's UniGCS switches "AI recognition push" off (spec section 4
                // mode 0x00), and a model reload clears it too (section 3 [12]). The pushes just
                // stop. The TCP link stays up on keep-alives, so nothing reconnects and nothing
                // re-opens counting - the state query only ever went out in _countLinkOpened() -
                // and the delivery's headline number dies for the rest of the flight. This bare
                // query answers with the flag; a "counting is off" answer arms the Start above.
                _sendCount(SiyiAi::PrivateCommandId::ObjectCount);
            }
        }
    }

    if ((_pollTicks % kStatusInterval) == 0) {
        _send(SiyiAi::encodeRequest(SiyiAi::CommandId::RequestRecognitionState, _sequence++));
        if (!_streamRequested) {
            // Open both streams once; afterwards only re-open on reported closure. The video one
            // is what makes the module's RTSP actually carry frames on a UDP control link.
            _send(SiyiAi::encodeSetVideoStream(true, _sequence++));
            _send(SiyiAi::encodeSetTargetStream(true, _sequence++));
            _streamRequested = true;
        } else {
            _send(SiyiAi::encodeRequest(SiyiAi::CommandId::RequestTargetStreamState, _sequence++));
        }
    }
}

void SiyiAiController::_setConnected(bool connected)
{
    if (_connected == connected) {
        return;
    }
    _connected = connected;
    if (!_connected) {
        // The refusal belongs to the session that was refused. A module that reboots, or a link
        // that blips, otherwise carries "video too large" through the rest of the flight and hides
        // the tracking state behind it on a module that is now working.
        _setStreamTooLarge(false);

        // Same reason, and it reaches the strip: the AI button reads this flag, so a frozen true
        // keeps the button lit and reading "recognition is running" over a module we can no longer
        // reach - and pressing it then sends nothing, because the send path is itself gated on the
        // link. Only the module can say recognition is on, so with no module we do not claim it.
        if (_recognitionEnabled) {
            _recognitionEnabled = false;
            emit recognitionEnabledChanged();
        }
    }
    qCDebug(SiyiAiControllerLog) << "connected:" << _connected;
    emit connectedChanged();
}

void SiyiAiController::_setHasTarget(bool hasTarget)
{
    if (_hasTarget == hasTarget) {
        return;
    }
    _hasTarget = hasTarget;
    emit targetChanged();
}

void SiyiAiController::_setStreamTooLarge(bool tooLarge)
{
    if (_streamTooLarge == tooLarge) {
        return;
    }
    _streamTooLarge = tooLarge;
    if (_streamTooLarge) {
        qCWarning(SiyiAiControllerLog) << "module refused recognition: input video is above 1920x1080";
    }
    emit streamTooLargeChanged();
}

void SiyiAiController::_sendCount(SiyiAi::PrivateCommandId command, const QByteArray &payload)
{
    if (!_countSocket || (_countSocket->state() != QAbstractSocket::ConnectedState)) {
        return;
    }
    (void) _countSocket->write(SiyiLongProtocol::encode(static_cast<quint8>(command), payload, _countSequence++));
}

void SiyiAiController::_readCountLink()
{
    if (!_countSocket) {
        return;
    }

    _countRxBuffer.append(_countSocket->readAll());

    // Decoding on every read is what keeps the buffer bounded; see SiyiLongProtocol::decode.
    const QList<SiyiLongProtocol::Frame> frames = SiyiLongProtocol::decode(_countRxBuffer);
    for (const SiyiLongProtocol::Frame &frame : frames) {
        _handleCountFrame(frame);
    }
}

void SiyiAiController::_countLinkOpened()
{
    qCDebug(SiyiAiControllerLog) << "count link open on" << _moduleAddress << SiyiAi::kPrivatePort;

    _countRxBuffer.clear();
    _countStartAckPending = false;
    _classNames.clear();
    _personClasses.clear();
    _vehicleClasses.clear();
    _fireClasses.clear();
    _smokeClasses.clear();
    _boatClasses.clear();
    _classModelId = -1;

    // Class list first: the tallies that follow are positional and mean nothing without it. Then
    // ask whether counting is already on rather than switching it on blind - the module's flag
    // survives a dropped client, so a reconnect usually needs no write at all.
    _sendCount(SiyiAi::PrivateCommandId::ObjectCount, objectCountPayload(SiyiAi::ObjectCountMode::ClassList));
    _sendCount(SiyiAi::PrivateCommandId::ObjectCount);
}

void SiyiAiController::_handleCountFrame(const SiyiLongProtocol::Frame &frame)
{
    if (static_cast<SiyiAi::PrivateCommandId>(frame.commandId) != SiyiAi::PrivateCommandId::ObjectCount) {
        qCDebug(SiyiAiControllerLog) << "unhandled private command:" << static_cast<int>(frame.commandId);
        return;
    }
    if (frame.data.isEmpty()) {
        return;
    }

    if (static_cast<quint8>(frame.data.at(0)) == static_cast<quint8>(SiyiAi::ObjectCountMode::ClassList)) {
        const QStringList names = SiyiAi::parseObjectClassNames(frame.data);
        if (names.isEmpty()) {
            qCWarning(SiyiAiControllerLog) << "class list unreadable, counts stay unreported";
            return;
        }

        _classNames = names;
        _personClasses.clear();
        _vehicleClasses.clear();
        _fireClasses.clear();
        _smokeClasses.clear();
        _boatClasses.clear();
        for (int index = 0; index < names.size(); ++index) {
            if (isPersonClass(names.at(index))) {
                _personClasses.append(index);
            } else if (isVehicleClass(names.at(index))) {
                _vehicleClasses.append(index);
            } else if (isFireClass(names.at(index))) {
                _fireClasses.append(index);
            } else if (isSmokeClass(names.at(index))) {
                _smokeClasses.append(index);
            } else if (isBoatClass(names.at(index))) {
                _boatClasses.append(index);
            }
        }
        _classModelId = static_cast<quint8>(frame.data.at(1));
        qCDebug(SiyiAiControllerLog) << "module classes" << names << "model" << _classModelId
                                     << "person" << _personClasses << "vehicle" << _vehicleClasses
                                     << "fire" << _fireClasses << "smoke" << _smokeClasses
                                     << "boat" << _boatClasses;
        if (_personClasses.isEmpty()) {
            // Nobody has read a real module's class list yet, so the names matched here are the
            // COCO ones and a guess. A model that says "pedestrian", "human" or a Chinese name
            // instead lands here, and the whole list is logged because that turns the field fix
            // into one string in isPersonClass(). A model with no vehicle class is ordinary and
            // says nothing, so it is not worth a warning.
            qCWarning(SiyiAiControllerLog)
                << "module class list names no person class, person count reads unknown; classes:" << names;
        }
        return;
    }

    const auto report = SiyiAi::parseObjectCountReport(frame.data);
    if (!report) {
        return;
    }
    if (!report->counting) {
        // Wanted, not written. Writing the Start from here made every "counting is off" report
        // produce another Start, and the module answers a Start it will not honour with another
        // "counting is off" - the spec's own failure mode, since the link has no version
        // negotiation and a firmware may renumber the modes or empty the handler the way it
        // emptied 0x90. That is a request/reply ping-pong at LAN round-trip time for the whole
        // flight. _poll()'s keep-alive tick sends it instead, which puts a floor of
        // kCountKeepAliveInterval under it - the same place, and for the same reason, as the
        // class-list re-read.
        _countStartWanted = true;
        return;
    }
    _countStartWanted = false;
    if (report->counts.isEmpty()) {
        // The state reply, or a push cut short: the switch without the tallies.
        return;
    }

    // Byte 1 of every reply and push is the model the module currently has loaded (spec section
    // 4), and the tally row after it is positional in that model's class order. _applyCounts()
    // only checks how many tallies there are, so a swap to a different model with the same class
    // count walks through it and index 0 goes on being summed as "person" after it stopped meaning
    // one - and this link has two clients, the hand controller's UniGCS being the other (the same
    // fact kCountReconnectInterval is written on), so the swap can happen with nothing on this
    // side asking for it. Dropped exactly as a count mismatch is: the keep-alive tick re-reads the
    // list and the card reads "-" until the two agree again.
    //
    // Tested here rather than on arrival, because only a frame that carries a tally row can put a
    // wrong number on the dashboard. A short or unreadable frame names a model too, and blanking
    // the card on one would hand a firmware that garbles a field the power to erase good counts;
    // those are already left to fall through to the last-good-numbers path above.
    if (!_classNames.isEmpty() && (static_cast<quint8>(frame.data.at(1)) != _classModelId)) {
        qCDebug(SiyiAiControllerLog) << "counts are for model" << static_cast<quint8>(frame.data.at(1))
                                     << "but the class list is model" << _classModelId
                                     << ", re-reading the list";
        _dropClassMapping();
        return;
    }

    if (_countStartAckPending && std::all_of(report->counts.cbegin(), report->counts.cend(),
                                             [](int count) { return count == 0; })) {
        // The ack to a Start is a tally row of class_count zero bytes (spec section 4:
        // {0x01, cur_model, N, 0...}), and applying it would put a confident 0 on the dashboard
        // before the module has run a single inference.
        //
        // Matched on that row of zeroes rather than on being the next frame to arrive. Arrival
        // order proves nothing here: the hand controller and UniGCS share this link (many clients
        // at once - see kCountReconnectInterval), so either can switch counting on in the same
        // instant and a push carrying real people can overtake our ack. Discarding that push and
        // then applying the ack behind it prints "0 people" from the middle of a crowd. Frame
        // sequence would be the exact match, but nothing was ever established about the module
        // echoing a request's sequence in its ack, and the CTRL byte is 0x02 on pushes and acks
        // alike (spec section 2), so neither distinguishes them. A genuinely empty scene loses one
        // frame to this and no more.
        _countStartAckPending = false;
        return;
    }
    // A non-zero row is somebody's real inference, so it is applied and the wait for the ack's
    // zero row carries on behind it.
    _applyCounts(report->counts);
}

void SiyiAiController::_applyCounts(const QList<int> &counts)
{
    if (_classNames.isEmpty()) {
        return;
    }
    if (counts.size() != _classNames.size()) {
        // A different model is loaded, so every index just changed meaning. Drop the mapping and
        // read it again rather than counting cars as people for the rest of the flight.
        // Not re-read from here. A module whose push and class list disagree permanently - the
        // link carries no version, so a firmware that renumbers one and not the other is the
        // documented failure mode - would answer the re-request with the same mismatched list and
        // drive request/reply at LAN round-trip time, a few milliseconds, for the rest of the
        // flight. _poll()'s keep-alive tick already re-asks whenever _classNames is empty, which
        // puts a floor of kCountKeepAliveInterval on it. Until it agrees the card reads "-".
        qCDebug(SiyiAiControllerLog) << "module now reports" << counts.size() << "classes, re-reading the list";
        _dropClassMapping();
        return;
    }

    const auto tally = [&counts](const QList<int> &classes, bool &saturated) {
        saturated = false;
        if (classes.isEmpty()) {
            // The module named no class of this kind, so there is nothing to sum and the sum of
            // nothing is zero - which on a dashboard reads as "none in view". Report unknown
            // instead and let the UI draw a dash; see the header.
            return -1;
        }
        int total = 0;
        for (const int index : classes) {
            total += counts.at(index);
            saturated = saturated || (counts.at(index) >= SiyiAi::kObjectCountSaturation);
        }
        return total;
    };

    bool personSaturated = false;
    bool vehicleSaturated = false;
    bool fireSaturated = false;
    bool smokeSaturated = false;
    bool boatSaturated = false;
    const int persons = tally(_personClasses, personSaturated);
    const int vehicles = tally(_vehicleClasses, vehicleSaturated);
    const int fires = tally(_fireClasses, fireSaturated);
    const int smokes = tally(_smokeClasses, smokeSaturated);
    const int boats = tally(_boatClasses, boatSaturated);

    _lastCountTimer.restart();

    const bool changed = !_countsValid || (persons != _personCount) || (vehicles != _vehicleCount) ||
                         (fires != _fireCount) || (smokes != _smokeCount) || (boats != _boatCount) ||
                         (personSaturated != _personSaturated) || (vehicleSaturated != _vehicleSaturated) ||
                         (fireSaturated != _fireSaturated) || (smokeSaturated != _smokeSaturated) ||
                         (boatSaturated != _boatSaturated);
    _personCount = persons;
    _vehicleCount = vehicles;
    _fireCount = fires;
    _smokeCount = smokes;
    _boatCount = boats;
    _personSaturated = personSaturated;
    _vehicleSaturated = vehicleSaturated;
    _fireSaturated = fireSaturated;
    _smokeSaturated = smokeSaturated;
    _boatSaturated = boatSaturated;
    _countsValid = true;

    if (changed) {
        emit countsChanged();
    }
}

void SiyiAiController::_dropClassMapping()
{
    _classNames.clear();
    _personClasses.clear();
    _vehicleClasses.clear();
    _fireClasses.clear();
    _smokeClasses.clear();
    _boatClasses.clear();
    _classModelId = -1;
    _setCountsValid(false);
}

void SiyiAiController::_setCountsValid(bool valid)
{
    if (_countsValid == valid) {
        return;
    }
    _countsValid = valid;
    emit countsChanged();
}
