#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtNetwork/QHostAddress>
#include <QtQmlIntegration/QtQmlIntegration>

class QAudioSource;
class QIODevice;
class QJSEngine;
class QQmlEngine;
class QTimer;
class QUdpSocket;

Q_DECLARE_LOGGING_CATEGORY(PoliceLiveVoiceLog)

/// \brief Push-to-talk from the handset's microphone to the loudspeaker payload over IP.
///
/// Meant for the aircraft's LTE modem through Tailscale: the SIYI datalink that carries the stored
/// messages runs at 57600 baud, a fifth of what live speech needs. The payload daemon already plays
/// 16 kHz 16-bit mono PCM sent to its audio port in 20 ms datagrams (magic A5 5B, then a
/// little-endian sequence number), and answers state requests on its command port, which is how the
/// panel shows whether the path is up before anyone talks.
///
/// Held, never latched: the microphone closes on release, when the application leaves the
/// foreground, or after kMaxTalkMsecs.
class PoliceLiveVoice : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool talking READ talking NOTIFY talkingChanged)
    Q_PROPERTY(bool reachable READ reachable NOTIFY reachableChanged)
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)

public:
    /// No default argument, for the reason TakeoffCounter gives: a default-constructible
    /// QML_SINGLETON is default-constructed by the engine instead of going through create().
    explicit PoliceLiveVoice(QObject *parent);
    ~PoliceLiveVoice() override;

    static PoliceLiveVoice *instance();
    static PoliceLiveVoice *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    Q_INVOKABLE void startTalking();
    Q_INVOKABLE void stopTalking();

    [[nodiscard]] bool talking() const { return _talking; }
    [[nodiscard]] bool reachable() const { return _reachable; }
    [[nodiscard]] qreal level() const { return _level; }
    [[nodiscard]] QString status() const { return _status; }
    [[nodiscard]] QString host() const { return _host; }
    void setHost(const QString &host);

    /// The payload's command port; live audio goes to the port after it, as the daemon expects.
    static constexpr quint16 kCommandPort = 37270;
    static constexpr quint16 kAudioPort = kCommandPort + 1;
    /// The voice relay app on this handset, which forwards both ports to the payload's Tailscale
    /// address. The station itself is kept out of Tailscale: a VPN that covers it stops the KCMVP
    /// bridge from tying its sockets to the USB Ethernet network.
    static constexpr char kDefaultHost[] = "127.0.0.1";

signals:
    void talkingChanged();
    void reachableChanged();
    void levelChanged();
    void statusChanged();
    void hostChanged();

private:
    void _openMicrophone();
    void _closeMicrophone();
    void _readMicrophone();
    void _process(float sample);
    void _sendFrame();
    void _probe();
    void _readReplies();
    void _parseHost();
    void _setReachable(bool reachable);
    void _setStatus(const QString &status);
    void _refreshIdleStatus();
    void _resetDsp();

    /// Second-order section, transposed direct form II.
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float run(float x);
        void reset() { z1 = z2 = 0.0f; }
    };
    static Biquad _lowPass(double rate, double cutoff, double q);
    static Biquad _highPass(double rate, double cutoff, double q);

    QUdpSocket *_socket = nullptr;
    QTimer *_probeTimer = nullptr;
    QTimer *_talkLimit = nullptr;
    QAudioSource *_source = nullptr;
    QIODevice *_input = nullptr;

    QString _host;
    QHostAddress _address;
    bool _talking = false;
    bool _reachable = false;
    qreal _level = 0.0;
    QString _status;
    QElapsedTimer _lastReply;
    quint16 _probeSequence = 0;

    // Capture to 16 kHz mono: anti-alias, then linear interpolation at the source rate's step.
    int _sourceRate = 16000;
    double _step = 1.0;
    double _phase = 0.0;
    float _previous = 0.0f;
    Biquad _antiAlias[2];
    QByteArray _partialFrame;  ///< Bytes of an incomplete multichannel sample from the last read.

    // Speech shaping at 16 kHz: rumble cut, slow automatic gain, soft limit.
    Biquad _rumble;
    float _envelope = 0.0f;
    float _gain = 1.0f;
    float _framePeak = 0.0f;

    QByteArray _frame;  ///< PCM of the datagram being filled.
    quint16 _audioSequence = 0;
    int _framesSinceLevel = 0;
    qint64 _framesSent = 0;
};
