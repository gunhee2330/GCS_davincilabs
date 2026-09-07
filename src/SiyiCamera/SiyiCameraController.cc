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

/// Poll tick counts, in units of kPollIntervalMs.
constexpr int kAttitudeInterval = 2;      ///< 5 Hz
constexpr int kConfigInterval = 10;       ///< 1 Hz
constexpr int kRangefinderInterval = 5;   ///< 2 Hz
constexpr int kThermalInterval = 10;      ///< 1 Hz
constexpr int kIdentityInterval = 20;     ///< Retry firmware/model discovery at 0.5 Hz.

} // namespace

Q_APPLICATION_STATIC(SiyiCameraController, _siyiCameraControllerInstance);

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

    if (_socket) {
        if ((_yawRate != 0) || (_pitchRate != 0)) {
            _send(SiyiProtocol::encodeGimbalRotation(0, 0, _sequence++));
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

void SiyiCameraController::_send(const QByteArray &packet)
{
    if (!_socket) {
        return;
    }
    if (_socket->writeDatagram(packet, _cameraAddress, _cameraPort) < 0) {
        qCWarning(SiyiCameraControllerLog) << "send failed:" << _socket->errorString();
    }
}

void SiyiCameraController::_sendCommand(SiyiProtocol::CommandId commandId, const QByteArray &data)
{
    _send(SiyiProtocol::encode(commandId, data, _sequence++));
}

void SiyiCameraController::_sendSingleByte(SiyiProtocol::CommandId commandId, quint8 value)
{
    _send(SiyiProtocol::encodeSingleByte(commandId, value, _sequence++));
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
            _sendCommand(SiyiProtocol::CommandId::ReadRangefinder);
            _sendCommand(SiyiProtocol::CommandId::ReadRangefinderTarget);
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

    _lastFrameTimer.invalidate();
    _lastRangefinderTimer.invalidate();
    _lastRangefinderTargetTimer.invalidate();
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
