#include "SiyiAiController.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QVariant>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QJSEngine>

#include "Fact.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "SiyiCameraSettings.h"
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

quint16 toReference(double normalised, int span)
{
    return static_cast<quint16>(qRound(qBound(0.0, normalised, 1.0) * (span - 1)));
}

} // namespace

Q_APPLICATION_STATIC(SiyiAiController, _siyiAiControllerInstance);

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

    qCDebug(SiyiAiControllerLog) << "connecting to" << _moduleAddress << _modulePort;

    _rxBuffer.clear();
    _pollTicks = 0;
    _streamRequested = false;
    _lastFrameTimer.start();
    _lastTargetTimer.invalidate();
    _pollTimer.start();

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

    _rxBuffer.clear();
    _streamRequested = false;
    _setHasTarget(false);
    _setConnected(false);
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
    // Selection coordinates are in the video stream's own resolution, not the reference
    // frame the target stream reports in.
    _send(SiyiAi::encodeTrackPoint(toReference(x, _streamWidth),
                                   toReference(y, _streamHeight),
                                   _sequence++));
}

void SiyiAiController::trackBox(double left, double top, double right, double bottom)
{
    _send(SiyiAi::encodeTrackBox(toReference(left, _streamWidth),
                                 toReference(top, _streamHeight),
                                 toReference(right, _streamWidth),
                                 toReference(bottom, _streamHeight),
                                 _sequence++));
}

void SiyiAiController::cancelTracking()
{
    _send(SiyiAi::encodeCancelTracking(_sequence++));
    _setHasTarget(false);
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
    case SiyiAi::CommandId::RequestRecognitionState:
    case SiyiAi::CommandId::SetRecognitionState: {
        const auto enabled = SiyiAi::parseEnabledFlag(frame.data);
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
            _hasTarget = true;
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

    if ((_pollTicks % kStatusInterval) == 0) {
        _send(SiyiAi::encodeRequest(SiyiAi::CommandId::RequestRecognitionState, _sequence++));
        if (!_streamRequested) {
            // Open the coordinate stream once; afterwards only re-open on reported closure.
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
