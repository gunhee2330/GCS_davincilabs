#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtQmlIntegration/QtQmlIntegration>

#include "SpeakerProtocol.h"

Q_DECLARE_LOGGING_CATEGORY(SpeakerControllerLog)

class QQmlEngine;
class QJSEngine;
class QUdpSocket;

/// \brief Drives a loudspeaker payload over UDP on the aircraft network.
///
/// The payload holds the audio files and does the playing; this class only selects a track
/// and reports what the payload says it is doing. It deliberately does not go through the
/// flight controller: a broadcast has to stay available when the vehicle link degrades, and
/// routing through the autopilot would tie the loudspeaker to the autopilot's health.
class SpeakerController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool     connected    READ connected    NOTIFY connectedChanged)
    Q_PROPERTY(bool     playing      READ playing      NOTIFY stateChanged)
    Q_PROPERTY(int      currentTrack READ currentTrack NOTIFY stateChanged)
    Q_PROPERTY(int      volume       READ volume       NOTIFY stateChanged)
    Q_PROPERTY(int      trackCount   READ trackCount   NOTIFY stateChanged)

    /// Button labels for the stored audio files, from settings. Index 0 is track 1.
    Q_PROPERTY(QStringList messageNames READ messageNames NOTIFY messageNamesChanged)

public:
    explicit SpeakerController(QObject *parent = nullptr);
    ~SpeakerController();

    static SpeakerController *instance();
    static SpeakerController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    /// Starts the link if the payload is enabled and keeps it following the settings. Call
    /// once, after SettingsManager::init().
    void init();

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    /// Track numbers are 1 based, matching the payload's file ordering.
    Q_INVOKABLE void play(int track);
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void setVolume(int percent);

    [[nodiscard]] bool connected() const { return _connected; }
    [[nodiscard]] bool playing() const { return _state.playing; }
    [[nodiscard]] int currentTrack() const { return _state.track; }
    [[nodiscard]] int volume() const { return _state.volume; }
    [[nodiscard]] int trackCount() const { return _state.trackCount; }
    [[nodiscard]] QStringList messageNames() const { return _messageNames; }

signals:
    void connectedChanged();
    void stateChanged();
    void messageNamesChanged();

private slots:
    void _readPendingDatagrams();
    void _poll();

private:
    void _send(const QByteArray &packet);
    void _handleFrame(const SpeakerProtocol::Frame &frame);
    void _setConnected(bool connected);
    void _reloadMessageNames();

    QUdpSocket *_socket = nullptr;
    QTimer _pollTimer;
    QHostAddress _address;
    quint16 _port = 0;
    QByteArray _rxBuffer;
    quint16 _sequence = 0;

    QElapsedTimer _lastFrameTimer;
    bool _connected = false;

    SpeakerProtocol::State _state;
    QStringList _messageNames;
};
