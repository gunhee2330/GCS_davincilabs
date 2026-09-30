#include "PoliceLiveVoice.h"

#include <algorithm>
#include <cmath>

#include <QtCore/QApplicationStatic>
#include <QtCore/QCoreApplication>
#include <QtCore/QPermissions>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtCore/QtEndian>
#include <QtGui/QGuiApplication>
#include <QtMultimedia/QAudioDevice>
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QMediaDevices>
#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QJSEngine>

#include "QGCLoggingCategory.h"
#include "SpeakerProtocol.h"

QGC_LOGGING_CATEGORY(PoliceLiveVoiceLog, "PoliceDrone.LiveVoice")

namespace {

constexpr int kOutputRate = 16000;
/// 20 ms at 16 kHz, 16-bit mono: the frame the daemon's live audio loop is written for.
constexpr int kFrameSamples = kOutputRate / 50;
constexpr int kFrameBytes = kFrameSamples * 2;
constexpr char kAudioMagic[2] = {'\xA5', '\x5B'};

/// A state request every few seconds is all the path check costs: a few dozen bytes each way.
constexpr int kProbeIntervalMsecs = 3000;
/// Two missed replies and a margin before the panel calls the path down.
constexpr qint64 kReplyTimeoutMsecs = 7500;
/// Letting go is what ends a broadcast; this is only the backstop for a release that never came.
constexpr int kMaxTalkMsecs = 5 * 60 * 1000;

/// Below this there are no words, only handling noise and wind, and the payload's small driver
/// spends excursion on it that the words need.
constexpr double kRumbleCutoffHz = 200.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kButterworthQ = 0.70710678118654752;
constexpr double kAntiAliasCutoffHz = 7000.0;

/// The gain rides speech up toward kTargetPeak, never past kMaxGain, and not at all while the
/// level is under kGateLevel, so the pauses between words are not pumped up to speech level.
constexpr float kTargetPeak = 0.6f;
constexpr float kMaxGain = 6.0f;           // +15.6 dB
constexpr float kGateLevel = 0.02f;        // -34 dBFS
constexpr float kAttackCoeff = 0.02f;      // envelope rise per sample at 16 kHz, about 3 ms
constexpr float kReleaseCoeff = 0.0002f;   // envelope fall, about 300 ms
constexpr float kGainDownCoeff = 0.01f;    // gain falls within about 6 ms
constexpr float kGainUpCoeff = 0.0003f;    // and comes back over about 200 ms
/// tanh soft limit: transparent at speech level, rounding peaks instead of clipping them.
constexpr float kDrive = 1.2f;

constexpr int kLevelEveryFrames = 5;  // ten level updates a second is plenty for a bar

const QString kSettingsGroup = QStringLiteral("PoliceLiveVoice");
const QString kHostKey = QStringLiteral("host");

}  // namespace

Q_APPLICATION_STATIC(PoliceLiveVoice, _policeLiveVoiceInstance, nullptr);

float PoliceLiveVoice::Biquad::run(float x)
{
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

PoliceLiveVoice::Biquad PoliceLiveVoice::_lowPass(double rate, double cutoff, double q)
{
    const double w0 = 2.0 * kPi * cutoff / rate;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double c = std::cos(w0);
    Biquad f;
    f.b0 = static_cast<float>((1.0 - c) / 2.0 / a0);
    f.b1 = static_cast<float>((1.0 - c) / a0);
    f.b2 = f.b0;
    f.a1 = static_cast<float>(-2.0 * c / a0);
    f.a2 = static_cast<float>((1.0 - alpha) / a0);
    return f;
}

PoliceLiveVoice::Biquad PoliceLiveVoice::_highPass(double rate, double cutoff, double q)
{
    const double w0 = 2.0 * kPi * cutoff / rate;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double c = std::cos(w0);
    Biquad f;
    f.b0 = static_cast<float>((1.0 + c) / 2.0 / a0);
    f.b1 = static_cast<float>(-(1.0 + c) / a0);
    f.b2 = f.b0;
    f.a1 = static_cast<float>(-2.0 * c / a0);
    f.a2 = static_cast<float>((1.0 - alpha) / a0);
    return f;
}

PoliceLiveVoice::PoliceLiveVoice(QObject *parent)
    : QObject(parent)
    , _socket(new QUdpSocket(this))
    , _probeTimer(new QTimer(this))
    , _talkLimit(new QTimer(this))
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    _host = settings.value(kHostKey, QString::fromLatin1(kDefaultHost)).toString().trimmed();
    settings.endGroup();

    // Bound so the payload's state replies have a port to come back to.
    if (!_socket->bind(QHostAddress::AnyIPv4, 0)) {
        qCWarning(PoliceLiveVoiceLog) << "bind failed:" << _socket->errorString();
    }
    (void) connect(_socket, &QUdpSocket::readyRead, this, &PoliceLiveVoice::_readReplies);

    _probeTimer->setInterval(kProbeIntervalMsecs);
    (void) connect(_probeTimer, &QTimer::timeout, this, &PoliceLiveVoice::_probe);
    _probeTimer->start();

    _talkLimit->setSingleShot(true);
    _talkLimit->setInterval(kMaxTalkMsecs);
    (void) connect(_talkLimit, &QTimer::timeout, this, [this]() {
        qCWarning(PoliceLiveVoiceLog) << "talk limit reached";
        stopTalking();
    });

    // Leaving the foreground (screen off, another app, the permission sheet) takes the touch with
    // it, and the release may never be delivered. Close the microphone rather than trust it.
    if (auto *const app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        (void) connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) {
                stopTalking();
            }
        });
    }

    _parseHost();
    _refreshIdleStatus();
    QTimer::singleShot(0, this, &PoliceLiveVoice::_probe);
}

PoliceLiveVoice::~PoliceLiveVoice()
{
    _closeMicrophone();
}

PoliceLiveVoice *PoliceLiveVoice::instance()
{
    return _policeLiveVoiceInstance();
}

PoliceLiveVoice *PoliceLiveVoice::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    PoliceLiveVoice *const voice = instance();
    QJSEngine::setObjectOwnership(voice, QJSEngine::CppOwnership);
    return voice;
}

void PoliceLiveVoice::setHost(const QString &host)
{
    const QString trimmed = host.trimmed();
    if (trimmed.isEmpty() || (trimmed == _host)) {
        return;
    }
    _host = trimmed;
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(kHostKey, _host);
    settings.endGroup();
    emit hostChanged();

    _setReachable(false);
    _parseHost();
    _refreshIdleStatus();
    _probe();
}

void PoliceLiveVoice::startTalking()
{
    if (_talking) {
        return;
    }

#if QT_CONFIG(permissions)
    const QMicrophonePermission microphone;
    const Qt::PermissionStatus permission = QCoreApplication::instance()->checkPermission(microphone);
    if (permission == Qt::PermissionStatus::Undetermined) {
        // The system sheet takes the touch, so the press that asked cannot also be the one
        // that talks: grant, then press again.
        QCoreApplication::instance()->requestPermission(microphone, this, [this](const QPermission &result) {
            _setStatus((result.status() == Qt::PermissionStatus::Granted)
                           ? tr("마이크를 허용했습니다. 다시 누르고 말하세요")
                           : tr("마이크 권한이 거부되었습니다"));
        });
        return;
    }
    if (permission != Qt::PermissionStatus::Granted) {
        _setStatus(tr("마이크 권한이 없습니다. 안드로이드 설정에서 허용하세요"));
        return;
    }
#endif

    if (_address.isNull()) {
        _setStatus(tr("음성 중계 주소를 확인하세요"));
        return;
    }

    _openMicrophone();
}

void PoliceLiveVoice::stopTalking()
{
    if (!_talking) {
        return;
    }

    _closeMicrophone();

    // The tail of the last word: pad the part-filled datagram out rather than drop it.
    if (!_frame.isEmpty()) {
        _frame.append(QByteArray(kFrameBytes - _frame.size(), '\0'));
        _sendFrame();
    }
    qCDebug(PoliceLiveVoiceLog) << "frames sent:" << _framesSent;

    _talking = false;
    _talkLimit->stop();
    _level = 0.0;
    emit levelChanged();
    emit talkingChanged();
    _refreshIdleStatus();
}

void PoliceLiveVoice::_openMicrophone()
{
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        _setStatus(tr("마이크를 찾을 수 없습니다"));
        return;
    }

    QAudioFormat format;
    format.setSampleRate(kOutputRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        // _readMicrophone() brings whatever the device prefers to 16 kHz mono.
        format = device.preferredFormat();
    }

    _source = new QAudioSource(device, format, this);
    _source->setBufferSize(format.bytesForDuration(40 * 1000));
    _input = _source->start();
    if (!_input || (_source->error() != QtAudio::NoError)) {
        qCWarning(PoliceLiveVoiceLog) << "microphone did not open:" << _source->error()
                                      << "format:" << format;
        _closeMicrophone();
        _setStatus(tr("마이크를 열 수 없습니다"));
        return;
    }
    (void) connect(_input, &QIODevice::readyRead, this, &PoliceLiveVoice::_readMicrophone);

    _sourceRate = format.sampleRate();
    _step = static_cast<double>(_sourceRate) / kOutputRate;
    _resetDsp();
    _framesSent = 0;
    qCDebug(PoliceLiveVoiceLog) << "address:" << _address
                                << "port:" << kAudioPort
                                << "format:" << format;

    _talking = true;
    _talkLimit->start();
    emit talkingChanged();
    _setStatus(_reachable ? tr("송신 중") : tr("송신 중 (스피커 응답은 아직 없음)"));
}

void PoliceLiveVoice::_closeMicrophone()
{
    if (_input) {
        (void) disconnect(_input, nullptr, this, nullptr);
        _input = nullptr;
    }
    if (_source) {
        _source->stop();
        _source->deleteLater();
        _source = nullptr;
    }
}

void PoliceLiveVoice::_resetDsp()
{
    _phase = 0.0;
    _previous = 0.0f;
    _partialFrame.clear();
    _frame.clear();
    _envelope = 0.0f;
    _gain = 1.0f;
    _framePeak = 0.0f;
    _framesSinceLevel = 0;

    // Fourth-order Butterworth as two sections, used only when the source rate is above 16 kHz.
    _antiAlias[0] = _lowPass(_sourceRate, std::min(kAntiAliasCutoffHz, 0.45 * _sourceRate), 0.5412);
    _antiAlias[1] = _lowPass(_sourceRate, std::min(kAntiAliasCutoffHz, 0.45 * _sourceRate), 1.3066);
    _rumble = _highPass(kOutputRate, kRumbleCutoffHz, kButterworthQ);
}

void PoliceLiveVoice::_readMicrophone()
{
    if (!_input || !_source) {
        return;
    }

    const QAudioFormat format = _source->format();
    const int frameBytes = format.bytesPerFrame();
    const int sampleBytes = format.bytesPerSample();
    const int channels = format.channelCount();
    if ((frameBytes <= 0) || (channels <= 0)) {
        return;
    }

    const QByteArray data = _partialFrame + _input->readAll();
    const qsizetype whole = (data.size() / frameBytes) * frameBytes;
    _partialFrame = data.mid(whole);

    const char *const bytes = data.constData();
    for (qsizetype offset = 0; offset < whole; offset += frameBytes) {
        float sample = 0.0f;
        for (int channel = 0; channel < channels; ++channel) {
            sample += format.normalizedSampleValue(bytes + offset + (channel * sampleBytes));
        }
        sample /= static_cast<float>(channels);

        if (_sourceRate == kOutputRate) {
            _process(sample);
            continue;
        }
        if (_sourceRate > kOutputRate) {
            sample = _antiAlias[1].run(_antiAlias[0].run(sample));
        }
        while (_phase <= 1.0) {
            _process(_previous + (static_cast<float>(_phase) * (sample - _previous)));
            _phase += _step;
        }
        _phase -= 1.0;
        _previous = sample;
    }
}

void PoliceLiveVoice::_process(float sample)
{
    const float x = _rumble.run(sample);

    const float magnitude = std::fabs(x);
    _envelope += ((magnitude > _envelope) ? kAttackCoeff : kReleaseCoeff) * (magnitude - _envelope);
    const float wanted = (_envelope < kGateLevel) ? 1.0f : std::clamp(kTargetPeak / _envelope, 1.0f, kMaxGain);
    _gain += ((wanted < _gain) ? kGainDownCoeff : kGainUpCoeff) * (wanted - _gain);

    const float y = std::clamp(std::tanh(x * _gain * kDrive) / std::tanh(kDrive), -1.0f, 1.0f);
    _framePeak = std::max(_framePeak, std::fabs(y));

    char pcm[2];
    qToLittleEndian(static_cast<qint16>(std::lround(y * 32767.0f)), pcm);
    _frame.append(pcm, 2);
    if (_frame.size() >= kFrameBytes) {
        _sendFrame();
    }
}

void PoliceLiveVoice::_sendFrame()
{
    QByteArray datagram;
    datagram.reserve(4 + kFrameBytes);
    datagram.append(kAudioMagic, 2);
    char sequence[2];
    qToLittleEndian(_audioSequence++, sequence);
    datagram.append(sequence, 2);
    datagram.append(_frame.constData(), kFrameBytes);
    _frame.remove(0, kFrameBytes);

    if (_socket->writeDatagram(datagram, _address, kAudioPort) < 0) {
        qCDebug(PoliceLiveVoiceLog) << "audio send failed:" << _socket->errorString();
    } else {
        ++_framesSent;
    }

    if (++_framesSinceLevel >= kLevelEveryFrames) {
        _framesSinceLevel = 0;
        _level = _framePeak;
        _framePeak = 0.0f;
        emit levelChanged();
    }
}

void PoliceLiveVoice::_probe()
{
    if (_lastReply.isValid() && (_lastReply.elapsed() > kReplyTimeoutMsecs)) {
        _setReachable(false);
    }
    if (_address.isNull()) {
        return;
    }
    (void) _socket->writeDatagram(SpeakerProtocol::encodeRequestState(_probeSequence++), _address, kCommandPort);
}

void PoliceLiveVoice::_readReplies()
{
    while (_socket->hasPendingDatagrams()) {
        QByteArray buffer = _socket->receiveDatagram().data();
        for (const SpeakerProtocol::Frame &frame : SpeakerProtocol::decode(buffer)) {
            if ((frame.commandId == static_cast<quint8>(SpeakerProtocol::CommandId::RequestState))
                && SpeakerProtocol::parseState(frame.data).has_value()) {
                _lastReply.start();
                _setReachable(true);
            }
        }
    }
}

void PoliceLiveVoice::_parseHost()
{
    _address = QHostAddress();
    QHostAddress address;
    if (address.setAddress(_host)) {
        _address = address;
        return;
    }

    qCWarning(PoliceLiveVoiceLog) << "not an IP address:" << _host;
}

void PoliceLiveVoice::_setReachable(bool reachable)
{
    if (reachable == _reachable) {
        return;
    }
    _reachable = reachable;
    emit reachableChanged();
    _refreshIdleStatus();
}

void PoliceLiveVoice::_setStatus(const QString &status)
{
    if (status == _status) {
        return;
    }
    _status = status;
    emit statusChanged();
}

void PoliceLiveVoice::_refreshIdleStatus()
{
    if (_talking) {
        return;
    }
    if (_address.isNull()) {
        _setStatus(tr("음성 중계 주소를 확인하세요"));
    } else if (_reachable) {
        _setStatus(tr("LTE 연결됨, 기체 스피커가 응답합니다"));
    } else {
        _setStatus(tr("LTE 응답 없음. 음성 중계 앱, 조종기 인터넷, Tailscale을 확인하세요"));
    }
}
