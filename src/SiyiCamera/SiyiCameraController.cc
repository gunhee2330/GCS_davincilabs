#include "SiyiCameraController.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QVariant>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QJSEngine>

#include "Fact.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "SiyiCameraSettings.h"

QGC_LOGGING_CATEGORY(SiyiCameraControllerLog, "SiyiCamera.SiyiCameraController")

namespace {

constexpr int kPollIntervalMs = 100;

/// The camera is declared offline once this long passes with no valid frame.
constexpr qint64 kConnectionTimeoutMs = 2000;
constexpr qint64 kRangefinderTimeoutMs = 1000;
constexpr qint64 kThermalRangeTimeoutMs = 3000;

/// How long a 0xC3 confirmation is worth showing as "following". Not a link timeout: it is the
/// age at which a past answer stops being evidence about the present, and there is no way to ask
/// again - a re-query is a re-assert, which would switch follow back on. Whether the gimbal ever
/// drops follow by itself, and whether it would send anything if it did, is unmeasured: spec
/// section 6 recovered only the codes it answers a request with, and the gimbal firmware has
/// never been disassembled. Long enough that an operator who just switched follow on is not told
/// it is unconfirmed.
constexpr qint64 kAiFollowStaleMs = 10000;

/// How many times a stop goes out, and how far apart in poll ticks. The link is UDP with no
/// retransmission of any kind and this is the command that gives the sticks back, so one datagram
/// is not enough. Repeating is taken to be safe rather than known to be: the residue read that
/// makes a repeated command dangerous was found in the AI module's apt_select_ai_target@0x577930
/// - a different device, and the documented 0x06 - while 0xC3 carries its whole meaning in one
/// 0/1 byte and the gimbal's handler for it has never been disassembled. Bounded because the
/// reply to a stop was never recovered either (spec section 6 lists 1 and 2..8 for a start): a
/// gimbal that echoes 1 would otherwise keep this running for the whole flight.
constexpr int kAiFollowStopSends = 3;
constexpr int kAiFollowStopIntervalTicks = 5;   ///< 500 ms, ~3x a LAN round trip.

/// Poll tick counts, in units of kPollIntervalMs.
constexpr int kAttitudeInterval = 2;      ///< 5 Hz
constexpr int kConfigInterval = 10;       ///< 1 Hz
constexpr int kRangefinderInterval = 5;   ///< 2 Hz
constexpr int kThermalInterval = 10;      ///< 1 Hz
constexpr int kIdentityInterval = 20;     ///< Retry firmware/model discovery at 0.5 Hz.

/// How many times the poll may ask the pod to light its laser before giving up on this link.
/// Enough to ride out a dropped datagram, and bounded because 0x31 is unverified hardware: a
/// pod that never answers it would otherwise leave _laserOn false and the poll re-sending
/// 0x32 at 2 Hz for the whole flight.
constexpr int kLaserOnAttempts = 5;

} // namespace

Q_APPLICATION_STATIC(SiyiCameraController, _siyiCameraControllerInstance, nullptr);

SiyiCameraController::SiyiCameraController(QObject *parent)
    : QObject(parent)
{
    _pollTimer.setInterval(kPollIntervalMs);
    (void) connect(&_pollTimer, &QTimer::timeout, this, &SiyiCameraController::_poll);
}

SiyiCameraController::~SiyiCameraController()
{
    stop();
}

SiyiCameraController *SiyiCameraController::instance()
{
    return _siyiCameraControllerInstance();
}

SiyiCameraController *SiyiCameraController::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    SiyiCameraController *const controller = instance();
    QJSEngine::setObjectOwnership(controller, QJSEngine::CppOwnership);
    return controller;
}

void SiyiCameraController::init()
{
    if (_initialized) {
        return;
    }

    SiyiCameraSettings *const settings = SettingsManager::instance()->siyiCameraSettings();
    if (!settings) {
        qCWarning(SiyiCameraControllerLog) << "settings unavailable";
        return;
    }

    const auto applySettings = [this, settings]() {
        if (settings->enabled()->rawValue().toBool()) {
            start();
        } else {
            stop();
        }
    };

    for (Fact *const fact : {settings->enabled(), settings->ipAddress(), settings->port()}) {
        (void) connect(fact, &Fact::rawValueChanged, this, [applySettings](const QVariant &) { applySettings(); });
    }

    _initialized = true;
    applySettings();
}

void SiyiCameraController::start()
{
    stop();

    SiyiCameraSettings *const settings = SettingsManager::instance()->siyiCameraSettings();
    if (!settings) {
        qCWarning(SiyiCameraControllerLog) << "settings unavailable";
        return;
    }

    const QString address = settings->ipAddress()->rawValue().toString();
    if (!_cameraAddress.setAddress(address)) {
        qCWarning(SiyiCameraControllerLog) << "invalid camera address:" << address;
        return;
    }
    _cameraPort = static_cast<quint16>(settings->port()->rawValue().toUInt());

    _socket = new QUdpSocket(this);
    if (!_socket->bind(QHostAddress::AnyIPv4, 0)) {
        qCWarning(SiyiCameraControllerLog) << "bind failed:" << _socket->errorString();
        delete _socket;
        _socket = nullptr;
        return;
    }
    (void) connect(_socket, &QUdpSocket::readyRead, this, &SiyiCameraController::_readPendingDatagrams);

    qCDebug(SiyiCameraControllerLog) << "connecting to" << _cameraAddress << _cameraPort;

    _rxBuffer.clear();
    _pollTicks = 0;
    _lastFrameTimer.start();
    _pollTimer.start();

    // Identify the pod straight away so model-gated features settle before first use.
    _sendCommand(SiyiProtocol::CommandId::AcquireHardwareId);
    _sendCommand(SiyiProtocol::CommandId::AcquireFirmwareVersion);
}

void SiyiCameraController::stop()
{
    _pollTimer.stop();

    bool stopSent = false;

    if (_socket) {
        if ((_yawRate != 0) || (_pitchRate != 0)) {
            _send(SiyiProtocol::encodeGimbalRotation(0, 0, _sequence++));
        }
        // Nothing else ever turns the laser off, and the pod keeps it lit across a client
        // going away, so quitting or unchecking the camera setting would leave a Class 3R
        // laser firing until the pod is power-cycled. Sent regardless of _laserOn: an
        // unanswered 0x31 leaves that false while the laser is in fact lit.
        if (isZT30()) {
            _send(SiyiProtocol::encodeSetLaserState(false, _sequence++));
        }
        // Follow goes off for exactly the reason the laser does, and it matters more: nothing in
        // the recovered protocol says the gimbal drops follow when this socket closes, and follow
        // is what flies the aircraft. Without this, quitting or unchecking the camera setting
        // leaves the airframe chasing a person with no screen and no operator. Keyed on the
        // request rather than on _aiFollowEnabled, which an unanswered 0xC3 leaves false while the
        // gimbal is in fact following.
        if (_aiFollowRequested || _aiFollowEnabled) {
            stopSent = _send(SiyiProtocol::encodeSingleByte(SiyiProtocol::CommandId::AiFollow, 0, _sequence++));
        }
        (void) disconnect(_socket, nullptr, this, nullptr);
        _socket->deleteLater();
        _socket = nullptr;
    }

    _yawRate = 0;
    _pitchRate = 0;
    _rxBuffer.clear();
    _setConnected(false);
    _resetCameraState();

    // Cleared here, unlike on the link timeout that _resetCameraState() handles: that one keeps it
    // so a delayed acceptance still counts when the link comes back, whereas this path has just
    // asked for follow off and has no socket left to hear an answer on.
    _aiFollowRequested = false;

    // Same reading setAiFollow(false) takes, for the same reason: a stop that left this process
    // means nothing on this side is asking for follow any more. Without it a settings edit -
    // init()'s applySettings calls start(), and start() begins with stop() - turns follow off on
    // the wire while _resetCameraState() above leaves the screen on "unconfirmed", so the banner
    // that says "follow is off and the aircraft is still in GUIDED" never lights and nobody is
    // told the aircraft is in GUIDED with nothing flying it. Runs after _resetCameraState()
    // because that one raises staleness.
    // Staleness is deliberately left standing. That single 0xC3 went out on UDP with the socket
    // torn down in the same function - no repeat, and nothing left to hear an answer on - so this
    // is the one stop path that cannot know it worked. setAiFollow(false) shows "unconfirmed" off
    // the same evidence after three tries; claiming certainty here off one unheard datagram would
    // paint the card a confident grey while the gimbal is still flying the aircraft.
    if (stopSent) {
        _aiFollowEnabled = false;
        _aiFollowStopSendsLeft = 0;
        _aiFollowStopState = AiFollowStop::StopUnconfirmed;
        emit aiFollowChanged();
    }

    // The poll that owes the stop repeats is gone, so stop claiming they are still coming - and do
    // not call it done either: whatever went out was never answered. Not in _resetCameraState():
    // that also runs on a link timeout, where the poll is still turning and the repeats are
    // exactly what should keep going.
    if ((_aiFollowStopSendsLeft > 0) || (_aiFollowStopState == AiFollowStop::StopPending)) {
        _aiFollowStopSendsLeft = 0;
        _aiFollowStopState = AiFollowStop::StopUnconfirmed;
        emit aiFollowChanged();
    }
}

void SiyiCameraController::rotate(int yawRate, int pitchRate)
{
    _yawRate = qBound(-100, yawRate, 100);
    _pitchRate = qBound(-100, pitchRate, 100);
    _send(SiyiProtocol::encodeGimbalRotation(_yawRate, _pitchRate, _sequence++));
}

void SiyiCameraController::center()
{
    _yawRate = 0;
    _pitchRate = 0;
    _sendSingleByte(SiyiProtocol::CommandId::Center, 1);
}

void SiyiCameraController::takePhoto()
{
    _sendSingleByte(SiyiProtocol::CommandId::PhotoAndMode, static_cast<quint8>(SiyiProtocol::PhotoFunction::TakePicture));
}

void SiyiCameraController::toggleRecording()
{
    _sendSingleByte(SiyiProtocol::CommandId::PhotoAndMode, static_cast<quint8>(SiyiProtocol::PhotoFunction::ToggleRecording));
    // The camera does not report recording state spontaneously.
    _sendCommand(SiyiProtocol::CommandId::AcquireConfigInfo);
}

void SiyiCameraController::toggleHdr()
{
    _sendSingleByte(SiyiProtocol::CommandId::PhotoAndMode, static_cast<quint8>(SiyiProtocol::PhotoFunction::ToggleHdr));
    _sendCommand(SiyiProtocol::CommandId::AcquireConfigInfo);
}

void SiyiCameraController::setMotionMode(int mode)
{
    SiyiProtocol::PhotoFunction function = SiyiProtocol::PhotoFunction::LockMode;
    switch (static_cast<SiyiProtocol::MotionMode>(mode)) {
    case SiyiProtocol::MotionMode::Lock:
        function = SiyiProtocol::PhotoFunction::LockMode;
        break;
    case SiyiProtocol::MotionMode::Follow:
        function = SiyiProtocol::PhotoFunction::FollowMode;
        break;
    case SiyiProtocol::MotionMode::Fpv:
        function = SiyiProtocol::PhotoFunction::FpvMode;
        break;
    default:
        qCWarning(SiyiCameraControllerLog) << "unknown motion mode:" << mode;
        return;
    }

    _sendSingleByte(SiyiProtocol::CommandId::PhotoAndMode, static_cast<quint8>(function));
    _sendCommand(SiyiProtocol::CommandId::AcquireConfigInfo);
}

void SiyiCameraController::zoom(int direction)
{
    _send(SiyiProtocol::encodeManualZoom(direction, _sequence++));
}

void SiyiCameraController::setZoom(double multiple)
{
    _send(SiyiProtocol::encodeAbsoluteZoom(static_cast<float>(multiple), _sequence++));
}

void SiyiCameraController::autoFocus()
{
    _sendSingleByte(SiyiProtocol::CommandId::AutoFocus, 1);
}

void SiyiCameraController::setCameraImageType(int imageType)
{
    if (!isZT30()) {
        qCWarning(SiyiCameraControllerLog) << "sensor routing is ZT30 only, model is" << _model;
        return;
    }
    _sendSingleByte(SiyiProtocol::CommandId::SetCameraImageType, static_cast<quint8>(imageType));

    if (_cameraImageType != imageType) {
        _cameraImageType = imageType;
        emit cameraImageTypeChanged();
    }
}

void SiyiCameraController::setThermalPalette(int palette)
{
    _sendSingleByte(SiyiProtocol::CommandId::SetThermalPalette, static_cast<quint8>(palette));
}

void SiyiCameraController::setThermalGain(int gain)
{
    _sendSingleByte(SiyiProtocol::CommandId::SetThermalGain, static_cast<quint8>(gain));
}

void SiyiCameraController::setAiFollow(bool on)
{
    // Payload is the single 0/1 byte UniGCS 3.1.6 sends (o/h.java O0(boolean)). Nothing is
    // gated here on purpose: the gimbal runs its own preconditions and answers with the reason
    // it refused, and a second set of checks on this side could only disagree with it.
    //
    // What is remembered is what was asked for. No confirmed way to tell which request a 0xC3
    // reply answers has been recovered - the spec leaves that unmeasured (section 8) - so a late
    // acceptance is indistinguishable from an acceptance of the newest send. Without this an
    // operator who slides follow on, waits, changes their mind and presses stop has the late
    // {0x01} arrive afterwards, and the dashboard reacts to it by putting the aircraft into
    // GUIDED - a flight mode change and dead sticks after an explicit cancel.
    _aiFollowRequested = on;

    const bool sent = _sendSingleByte(SiyiProtocol::CommandId::AiFollow, on ? 1 : 0);

    if (on) {
        // A new request starts from nothing the last one left behind. A refusal reason belongs to
        // the request that was refused: kept across a retry, an unanswered 0xC3 reads as "refused,
        // GPS data missing" and the operator goes on chasing a fix they already have, while the
        // panel's "the gimbal has to confirm it" line stays suppressed because it is gated on
        // there being no error.
        //
        // Staleness comes back with it. A confirmed stop leaves this flag false - that is what
        // makes the card say "off" rather than "unconfirmed" - and leaving it false through a new
        // request has the card go on saying "off" while a 0xC3{1} nobody answered is outstanding,
        // which is the same confident lie in the other direction. A gimbal too old to answer 0xC3
        // reproduces it every time.
        const bool cleared = (_aiFollowError != AiFollowError::None)
                             || (_aiFollowStopState != AiFollowStop::StopIdle)
                             || !_aiFollowStale;
        _aiFollowError = AiFollowError::None;
        _aiFollowStopState = AiFollowStop::StopIdle;
        _aiFollowStopSendsLeft = 0;
        _aiFollowStale = true;
        if (cleared) {
            emit aiFollowChanged();
        }
    } else {
        // A stop is reflected here and now rather than waited for. What aiFollowEnabled reports is
        // this side's belief that the gimbal is flying the aircraft, and after this call nothing on
        // this side is asking it to. Waiting for a reply that may never come - the gimbal can
        // ignore the stop, or answer code 1, which the guard above drops whole - left the start
        // slider hidden (its visible is driven by aiFollowEnabled) for the rest of the session,
        // with no way to re-engage follow. The dashboard's own GUIDED banner then reads "follow is
        // off and the aircraft is still in GUIDED", which is the sentence the operator needs.
        _aiFollowEnabled = false;
        // Off is only claimed for a stop that actually left this process. A stop that could not be
        // sent told the gimbal nothing, so follow is unknown, not off.
        _aiFollowStale = !sent;
        _aiFollowError = AiFollowError::None;
        _lastAiFollowTimer.invalidate();

        if (sent) {
            // One send has gone out; the poll owes the rest.
            _aiFollowStopSendsLeft = kAiFollowStopSends - 1;
            _aiFollowStopNextTick = _pollTicks + kAiFollowStopIntervalTicks;
            _aiFollowStopState = AiFollowStop::StopPending;
        } else {
            // The socket is gone, which also means the poll that would carry the repeats is
            // stopped. Promising retries that nothing will send, and calling that "waiting for the
            // gimbal", leaves the operator watching a reassuring line while the gimbal was never
            // told anything at all.
            _aiFollowStopSendsLeft = 0;
            _aiFollowStopState = AiFollowStop::StopUnsent;
        }
        emit aiFollowChanged();
    }
}

QString SiyiCameraController::recordingStatusText() const
{
    switch (_config.recordingStatus) {
    case SiyiProtocol::RecordingStatus::Off:
        return tr("Idle");
    case SiyiProtocol::RecordingStatus::On:
        return tr("Recording");
    case SiyiProtocol::RecordingStatus::NoCard:
        return tr("No SD card");
    case SiyiProtocol::RecordingStatus::DataLoss:
        return tr("Data loss");
    }
    return tr("Unknown");
}

bool SiyiCameraController::_send(const QByteArray &packet)
{
    if (!_socket) {
        return false;
    }
    if (_socket->writeDatagram(packet, _cameraAddress, _cameraPort) < 0) {
        qCWarning(SiyiCameraControllerLog) << "send failed:" << _socket->errorString();
        return false;
    }
    return true;
}

void SiyiCameraController::_sendCommand(SiyiProtocol::CommandId commandId, const QByteArray &data)
{
    _send(SiyiProtocol::encode(commandId, data, _sequence++));
}

bool SiyiCameraController::_sendSingleByte(SiyiProtocol::CommandId commandId, quint8 value)
{
    return _send(SiyiProtocol::encodeSingleByte(commandId, value, _sequence++));
}

void SiyiCameraController::_readPendingDatagrams()
{
    if (!_socket) {
        return;
    }

    while (_socket->hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(_socket->pendingDatagramSize()), Qt::Uninitialized);
        const qint64 read = _socket->readDatagram(datagram.data(), datagram.size());
        if (read < 0) {
            qCWarning(SiyiCameraControllerLog) << "read failed:" << _socket->errorString();
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

void SiyiCameraController::_handleFrame(const SiyiProtocol::Frame &frame)
{
    switch (frame.commandId) {
    case SiyiProtocol::CommandId::AcquireHardwareId: {
        const QString model = SiyiProtocol::parseHardwareModel(frame.data);
        if (!model.isEmpty() && (model != _model)) {
            _model = model;
            qCDebug(SiyiCameraControllerLog) << "model:" << _model;
            emit modelChanged();
        }
        break;
    }

    case SiyiProtocol::CommandId::AcquireFirmwareVersion: {
        const auto version = SiyiProtocol::parseFirmwareVersion(frame.data);
        if (version) {
            const QString text = version->zoom.isEmpty()
                ? tr("camera %1, gimbal %2").arg(version->camera, version->gimbal)
                : tr("camera %1, gimbal %2, zoom %3").arg(version->camera, version->gimbal, version->zoom);
            if (text != _firmwareVersion) {
                _firmwareVersion = text;
                emit firmwareVersionChanged();
            }
        }
        break;
    }

    case SiyiProtocol::CommandId::AcquireGimbalAttitude: {
        const auto attitude = SiyiProtocol::parseAttitude(frame.data);
        if (attitude) {
            _attitude = *attitude;
            emit attitudeChanged();
        }
        break;
    }

    case SiyiProtocol::CommandId::AcquireConfigInfo: {
        const auto config = SiyiProtocol::parseConfigInfo(frame.data);
        if (config) {
            _config = *config;
            emit configChanged();
        }
        break;
    }

    case SiyiProtocol::CommandId::ManualZoom: {
        const auto multiple = SiyiProtocol::parseZoomMultiple(frame.data);
        if (multiple) {
            _zoomMultiple = *multiple;
            emit zoomMultipleChanged();
        }
        break;
    }

    case SiyiProtocol::CommandId::ReadLaserState: {
        const auto state = SiyiProtocol::parseLaserState(frame.data);
        if (state) {
            if (*state) {
                // Lit, so stop asking for good. Otherwise an operator switching the laser off
                // on the SIYI hand controller would be overridden by the next poll tick.
                _laserOnAttemptsLeft = 0;
            }
            if (*state != _laserOn) {
                _laserOn = *state;
                emit laserStateChanged();
            }
        }
        break;
    }

    case SiyiProtocol::CommandId::SetLaserState:
        // The reply only says the pod took the command; the next 0x31 says what it did with it.
        _sendCommand(SiyiProtocol::CommandId::ReadLaserState);
        break;

    case SiyiProtocol::CommandId::ReadRangefinderTarget: {
        const auto target = SiyiProtocol::parseRangefinderTarget(frame.data);
        if (target) {
            const QGeoCoordinate coordinate(target->latDeg, target->lonDeg);
            _lastRangefinderTargetTimer.restart();
            if (coordinate != _rangefinderTarget) {
                _rangefinderTarget = coordinate;
                emit rangefinderTargetChanged();
            }
        }
        break;
    }

    case SiyiProtocol::CommandId::ReadRangefinder: {
        const auto distance = SiyiProtocol::parseRangefinderDistance(frame.data);
        if (distance) {
            _rangefinderDistance = *distance;
            _lastRangefinderTimer.restart();
            emit rangefinderDistanceChanged();
        }
        break;
    }

    case SiyiProtocol::CommandId::GetTempFullImage: {
        const auto range = SiyiProtocol::parseThermalRange(frame.data);
        if (range) {
            _thermalMaxTempC = range->maxTempC;
            _thermalMinTempC = range->minTempC;
            _lastThermalRangeTimer.restart();
            emit thermalRangeChanged();
        }
        break;
    }

    case SiyiProtocol::CommandId::AiFollow: {
        // Reply layout from UniGCS 3.1.6's e.java:732: at least one byte, the first being 1 when
        // the gimbal is now following and anything above that a refusal reason. Only this reply
        // moves the state, so a gimbal too old to know 0xC3 - which answers nothing at all -
        // leaves follow reading off.
        //
        // Length is not checked beyond emptiness, because e.java does not check it either
        // (if (i11 > 0)) and this pod family has form for a wider ack than the app reads: the
        // documented 0x04 answers two bytes (0x56d520, mov w2,#2). Demanding
        // exactly one would drop a {0x01, 0x00} whole and leave follow reading off for the flight
        // while the gimbal is following.
        if (frame.data.isEmpty()) {
            break;
        }
        const auto code = static_cast<quint8>(frame.data.at(0));
        const bool following = (code == 1);

        // An acceptance for a follow this side has already countermanded is dropped whole; see
        // setAiFollow(). Refusals are kept: they say the gimbal is not following, which agrees
        // with a cancel rather than contradicting it, and the reason is still worth showing.
        if (following && !_aiFollowRequested) {
            qCDebug(SiyiCameraControllerLog) << "ignoring follow acceptance for a cancelled request";
            break;
        }

        // Refusal codes are the enum values. The command is undocumented and unversioned, so a
        // firmware that grows a ninth reason must not be shown as one of the eight we can name.
        AiFollowError error = AiFollowError::None;
        if (code > 8) {
            error = AiFollowError::Unknown;
        } else if (code >= 2) {
            error = static_cast<AiFollowError>(code);
        }
        // The gimbal has said it is not following, which is what the stop was asking for. Any
        // repeat still owed is dropped here so the "waiting" text on the panel goes away.
        if (!following && (_aiFollowStopState != AiFollowStop::StopIdle)) {
            _aiFollowStopSendsLeft = 0;
            _aiFollowStopState = AiFollowStop::StopIdle;
            emit aiFollowChanged();
        }

        _lastAiFollowTimer.restart();
        if ((following != _aiFollowEnabled) || (error != _aiFollowError) || _aiFollowStale) {
            _aiFollowEnabled = following;
            _aiFollowError = error;
            _aiFollowStale = false;
            emit aiFollowChanged();
        }
        break;
    }

    case SiyiProtocol::CommandId::FunctionFeedbackInfo:
        if (!frame.data.isEmpty()) {
            _handleFunctionFeedback(static_cast<quint8>(frame.data.at(0)));
        }
        break;

    default:
        qCDebug(SiyiCameraControllerLog) << "unhandled command:" << static_cast<int>(frame.commandId);
        break;
    }
}

void SiyiCameraController::_handleFunctionFeedback(quint8 code)
{
    switch (code) {
    case 1:
        emit cameraError(tr("Camera failed to take a picture"));
        break;
    case 4:
        emit cameraError(tr("Camera failed to record video"));
        break;
    default:
        // 0 success, 2/3 HDR on/off - nothing to report.
        break;
    }
}

void SiyiCameraController::_poll()
{
    ++_pollTicks;

    if (_connected && (_lastFrameTimer.elapsed() > kConnectionTimeoutMs)) {
        if ((_yawRate != 0) || (_pitchRate != 0)) {
            _send(SiyiProtocol::encodeGimbalRotation(0, 0, _sequence++));
        }
        _yawRate = 0;
        _pitchRate = 0;
        _setConnected(false);
        // Identity goes with the link. A pod that comes back may have power-cycled into a
        // different image mode, and the dashboard only re-applies its sensor routing when the
        // model is announced again.
        _resetCameraState();
    }

    if (rangefinderTargetAvailable() && _lastRangefinderTargetTimer.hasExpired(kRangefinderTimeoutMs)) {
        _rangefinderTarget = QGeoCoordinate();
        emit rangefinderTargetChanged();
    }
    if (rangefinderAvailable() && _lastRangefinderTimer.hasExpired(kRangefinderTimeoutMs)) {
        _rangefinderDistance = std::numeric_limits<double>::quiet_NaN();
        emit rangefinderDistanceChanged();
    }
    if (thermalRangeAvailable() && _lastThermalRangeTimer.hasExpired(kThermalRangeTimeoutMs)) {
        _thermalMaxTempC = std::numeric_limits<double>::quiet_NaN();
        _thermalMinTempC = std::numeric_limits<double>::quiet_NaN();
        emit thermalRangeChanged();
    }
    if ((_aiFollowStopSendsLeft > 0) && (_pollTicks >= _aiFollowStopNextTick)) {
        --_aiFollowStopSendsLeft;
        _aiFollowStopNextTick = _pollTicks + kAiFollowStopIntervalTicks;
        _sendSingleByte(SiyiProtocol::CommandId::AiFollow, 0);
        if (_aiFollowStopSendsLeft == 0) {
            // Every repeat spent with nothing back. Whether the gimbal took the first one and did
            // not answer, or the link ate all three, cannot be told apart from here - but dropping
            // the line entirely put the panel back to its "the gimbal has to confirm it" start
            // text, which reads as a settled stop. Held until the gimbal answers or the operator
            // asks for something.
            _aiFollowStopState = AiFollowStop::StopUnconfirmed;
            emit aiFollowChanged();
        }
    }

    if (_aiFollowEnabled && !_aiFollowStale && _lastAiFollowTimer.isValid() &&
        _lastAiFollowTimer.hasExpired(kAiFollowStaleMs)) {
        // Deliberately only a flag: nothing is re-sent. 0xC3 answers when asked, and asking means
        // sending the command again, which would turn follow back on after the gimbal dropped it.
        _aiFollowStale = true;
        emit aiFollowChanged();
    }

    if ((_yawRate != 0) || (_pitchRate != 0)) {
        // The gimbal slews until told otherwise; repeating the rate keeps it moving smoothly
        // across a dropped datagram.
        _send(SiyiProtocol::encodeGimbalRotation(_yawRate, _pitchRate, _sequence++));
    }

    if ((_pollTicks % kAttitudeInterval) == 0) {
        _sendCommand(SiyiProtocol::CommandId::AcquireGimbalAttitude);
    }
    if ((_pollTicks % kConfigInterval) == 0) {
        _sendCommand(SiyiProtocol::CommandId::AcquireConfigInfo);
    }
    if ((_pollTicks % kIdentityInterval) == 0) {
        if (_model.isEmpty()) {
            _sendCommand(SiyiProtocol::CommandId::AcquireHardwareId);
        }
        if (_firmwareVersion.isEmpty()) {
            _sendCommand(SiyiProtocol::CommandId::AcquireFirmwareVersion);
        }
    }

    if (isZT30()) {
        if ((_pollTicks % kRangefinderInterval) == 0) {
            // An unlit laser answers 0x15/0x17 with zeroes, which both parsers refuse, so the
            // range and target readouts stay blank with nothing on screen saying why. The pod
            // powers up unlit and forgets across a power cycle, so light it here.
            if (!_laserOn && (_laserOnAttemptsLeft > 0)) {
                --_laserOnAttemptsLeft;
                _send(SiyiProtocol::encodeSetLaserState(true, _sequence++));
            }
            _sendCommand(SiyiProtocol::CommandId::ReadRangefinder);
            _sendCommand(SiyiProtocol::CommandId::ReadRangefinderTarget);
        }
        if ((_pollTicks % kConfigInterval) == 0) {
            _sendCommand(SiyiProtocol::CommandId::ReadLaserState);
        }
        if ((_pollTicks % kThermalInterval) == 0) {
            _send(SiyiProtocol::encodeThermalRangeRequest(_sequence++));
        }
    }
}

void SiyiCameraController::_resetCameraState()
{
    if (!_model.isEmpty()) {
        _model.clear();
        emit modelChanged();
    }
    if (!_firmwareVersion.isEmpty()) {
        _firmwareVersion.clear();
        emit firmwareVersionChanged();
    }

    if ((_attitude.yawDeg != 0.0F) || (_attitude.pitchDeg != 0.0F) || (_attitude.rollDeg != 0.0F)
        || (_attitude.yawRateDegPerSec != 0.0F) || (_attitude.pitchRateDegPerSec != 0.0F)
        || (_attitude.rollRateDegPerSec != 0.0F)) {
        _attitude = {};
        emit attitudeChanged();
    }

    if (_zoomMultiple != 1.0) {
        _zoomMultiple = 1.0;
        emit zoomMultipleChanged();
    }

    const SiyiProtocol::ConfigInfo defaultConfig;
    if ((_config.hdrEnabled != defaultConfig.hdrEnabled) || (_config.recordingStatus != defaultConfig.recordingStatus)
        || (_config.motionMode != defaultConfig.motionMode)
        || (_config.mountedUpsideDown != defaultConfig.mountedUpsideDown)) {
        _config = defaultConfig;
        emit configChanged();
    }

    if (rangefinderTargetAvailable()) {
        _rangefinderTarget = QGeoCoordinate();
        emit rangefinderTargetChanged();
    }
    if (rangefinderAvailable()) {
        _rangefinderDistance = std::numeric_limits<double>::quiet_NaN();
        emit rangefinderDistanceChanged();
    }
    if (thermalRangeAvailable()) {
        _thermalMaxTempC = std::numeric_limits<double>::quiet_NaN();
        _thermalMinTempC = std::numeric_limits<double>::quiet_NaN();
        emit thermalRangeChanged();
    }

    // Follow is deliberately NOT cleared with the rest of the pod state. Losing the link says
    // nothing about whether the gimbal is still flying the aircraft - nothing recovered ties
    // follow to this socket, and the SIYI air unit's setpoints do not come through it - so
    // painting follow "off" here would put a grey "off" on the detection card and a "follow is
    // off but the aircraft is in GUIDED" banner on the map while the aircraft is in fact still
    // chasing a person. A two second Wi-Fi hiccup is enough, and 0xC3 has no re-query, so that lie
    // would stand for the rest of the flight. Staleness is the honest reading: aiFollowStale
    // already renders as "unconfirmed". _aiFollowRequested is kept for the same reason -
    // dropping it would make the controller discard the acceptance that arrives when the link
    // comes back. Only a 0xC3 reply and an operator stop clear follow.
    if (_aiFollowEnabled && !_aiFollowStale) {
        _aiFollowStale = true;
        emit aiFollowChanged();
    }
    _lastAiFollowTimer.invalidate();

    _lastFrameTimer.invalidate();
    _lastRangefinderTimer.invalidate();
    _lastRangefinderTargetTimer.invalidate();
    _laserOnAttemptsLeft = kLaserOnAttempts;
    if (_laserOn) {
        _laserOn = false;
        emit laserStateChanged();
    }
    _lastThermalRangeTimer.invalidate();
}

void SiyiCameraController::_setConnected(bool connected)
{
    if (_connected == connected) {
        return;
    }
    _connected = connected;
    qCDebug(SiyiCameraControllerLog) << "connected:" << _connected;
    emit connectedChanged();
}
