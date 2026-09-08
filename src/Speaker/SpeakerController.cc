#include "SpeakerController.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QVariant>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QJSEngine>

#include "Fact.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "SpeakerSettings.h"

QGC_LOGGING_CATEGORY(SpeakerControllerLog, "Speaker.SpeakerController")

namespace {

constexpr int kPollIntervalMs = 1000;

/// The payload is declared offline once this long passes with no valid frame.
constexpr qint64 kConnectionTimeoutMs = 3000;

} // namespace

Q_APPLICATION_STATIC(SpeakerController, _speakerControllerInstance, nullptr);

SpeakerController::SpeakerController(QObject *parent)
    : QObject(parent)
{
    _pollTimer.setInterval(kPollIntervalMs);
    (void) connect(&_pollTimer, &QTimer::timeout, this, &SpeakerController::_poll);
}

SpeakerController::~SpeakerController()
{
    stop();
}

SpeakerController *SpeakerController::instance()
{
    return _speakerControllerInstance();
}

SpeakerController *SpeakerController::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    SpeakerController *const controller = instance();
    QJSEngine::setObjectOwnership(controller, QJSEngine::CppOwnership);
    return controller;
}

void SpeakerController::init()
{
    SpeakerSettings *const settings = SettingsManager::instance()->speakerSettings();
    if (!settings) {
        qCWarning(SpeakerControllerLog) << "settings unavailable";
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
    (void) connect(settings->messageNames(), &Fact::rawValueChanged, this, [this](const QVariant &) {
        _reloadMessageNames();
    });

    _reloadMessageNames();
    applySettings();
}

void SpeakerController::start()
{
    stop();

    SpeakerSettings *const settings = SettingsManager::instance()->speakerSettings();
    if (!settings) {
        qCWarning(SpeakerControllerLog) << "settings unavailable";
        return;
    }

    const QString address = settings->ipAddress()->rawValue().toString();
    if (!_address.setAddress(address)) {
        qCWarning(SpeakerControllerLog) << "invalid payload address:" << address;
        return;
    }
    _port = static_cast<quint16>(settings->port()->rawValue().toUInt());

    _socket = new QUdpSocket(this);
    if (!_socket->bind(QHostAddress::AnyIPv4, 0)) {
        qCWarning(SpeakerControllerLog) << "bind failed:" << _socket->errorString();
        delete _socket;
        _socket = nullptr;
        return;
    }
    (void) connect(_socket, &QUdpSocket::readyRead, this, &SpeakerController::_readPendingDatagrams);

    qCDebug(SpeakerControllerLog) << "connecting to" << _address << _port;

    _rxBuffer.clear();
    _lastFrameTimer.start();
    _pollTimer.start();

    _send(SpeakerProtocol::encodeRequestState(_sequence++));
}

void SpeakerController::stop()
{
    _pollTimer.stop();

    if (_socket) {
        _socket->deleteLater();
        _socket = nullptr;
    }

    _rxBuffer.clear();
    _state = {};
    emit stateChanged();
    _setConnected(false);
}

void SpeakerController::play(int track)
{
    if (track < 1) {
        return;
    }
    // Sent twice: a single lost datagram would otherwise leave the operator pressing a
    // button that appears to do nothing, and replaying a track already playing is harmless.
    const QByteArray packet = SpeakerProtocol::encodePlay(static_cast<quint8>(track), _sequence++);
    _send(packet);
    _send(SpeakerProtocol::encodePlay(static_cast<quint8>(track), _sequence++));
}

void SpeakerController::stopPlayback()
{
    _send(SpeakerProtocol::encodeStop(_sequence++));
    _send(SpeakerProtocol::encodeStop(_sequence++));
}

void SpeakerController::setVolume(int percent)
{
    _send(SpeakerProtocol::encodeSetVolume(static_cast<quint8>(qBound(0, percent, 100)), _sequence++));
}

void SpeakerController::_send(const QByteArray &packet)
{
    if (!_socket) {
        return;
    }
    if (_socket->writeDatagram(packet, _address, _port) < 0) {
        qCWarning(SpeakerControllerLog) << "send failed:" << _socket->errorString();
    }
}

void SpeakerController::_readPendingDatagrams()
{
    if (!_socket) {
        return;
    }

    while (_socket->hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(_socket->pendingDatagramSize()), Qt::Uninitialized);
        const qint64 read = _socket->readDatagram(datagram.data(), datagram.size());
        if (read < 0) {
            qCWarning(SpeakerControllerLog) << "read failed:" << _socket->errorString();
            return;
        }
        datagram.truncate(static_cast<int>(read));
        _rxBuffer.append(datagram);
    }

    const QList<SpeakerProtocol::Frame> frames = SpeakerProtocol::decode(_rxBuffer);
    for (const SpeakerProtocol::Frame &frame : frames) {
        _lastFrameTimer.restart();
        _setConnected(true);
        _handleFrame(frame);
    }
}

void SpeakerController::_handleFrame(const SpeakerProtocol::Frame &frame)
{
    switch (static_cast<SpeakerProtocol::CommandId>(frame.commandId)) {
    case SpeakerProtocol::CommandId::Play:
    case SpeakerProtocol::CommandId::Stop:
    case SpeakerProtocol::CommandId::SetVolume:
    case SpeakerProtocol::CommandId::RequestState: {
        const auto state = SpeakerProtocol::parseState(frame.data);
        if (state) {
            _state = *state;
            emit stateChanged();
        }
        break;
    }

    default:
        qCDebug(SpeakerControllerLog) << "unhandled command:" << static_cast<int>(frame.commandId);
        break;
    }
}

void SpeakerController::_poll()
{
    if (_connected && (_lastFrameTimer.elapsed() > kConnectionTimeoutMs)) {
        _setConnected(false);
        _state = {};
        emit stateChanged();
    }

    _send(SpeakerProtocol::encodeRequestState(_sequence++));
}

void SpeakerController::_setConnected(bool connected)
{
    if (_connected == connected) {
        return;
    }
    _connected = connected;
    qCDebug(SpeakerControllerLog) << "connected:" << _connected;
    emit connectedChanged();
}

void SpeakerController::_reloadMessageNames()
{
    SpeakerSettings *const settings = SettingsManager::instance()->speakerSettings();
    if (!settings) {
        return;
    }

    QStringList names;
    const QStringList raw = settings->messageNames()->rawValue().toString().split(QLatin1Char(','));
    for (const QString &name : raw) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty()) {
            names.append(trimmed);
        }
    }

    if (names != _messageNames) {
        _messageNames = names;
        emit messageNamesChanged();
    }
}
