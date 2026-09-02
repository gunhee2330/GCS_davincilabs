#pragma once

#include <cmath>
#include <limits>

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtQmlIntegration/QtQmlIntegration>

#include "SiyiProtocol.h"

Q_DECLARE_LOGGING_CATEGORY(SiyiCameraControllerLog)

class QQmlEngine;
class QJSEngine;
class QUdpSocket;

/// \brief Drives a SIYI optical pod over the vendor SDK protocol on UDP.
///
/// The camera is reached directly on its own Ethernet link (192.168.144.25:37260 by
/// default) rather than through the flight controller, so every ZT30 payload function is
/// available regardless of which autopilot is flying. Video is not handled here — the pod
/// publishes plain RTSP, which the existing QGC video pipeline already consumes.
class SiyiCameraController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool     connected           READ connected              NOTIFY connectedChanged)
    Q_PROPERTY(QString  model               READ model                  NOTIFY modelChanged)
    Q_PROPERTY(QString  firmwareVersion     READ firmwareVersion        NOTIFY firmwareVersionChanged)
    Q_PROPERTY(bool     isZT30              READ isZT30                 NOTIFY modelChanged)

    Q_PROPERTY(double   yawDeg              READ yawDeg                 NOTIFY attitudeChanged)
    Q_PROPERTY(double   pitchDeg            READ pitchDeg               NOTIFY attitudeChanged)
    Q_PROPERTY(double   rollDeg             READ rollDeg                NOTIFY attitudeChanged)

    Q_PROPERTY(double   zoomMultiple        READ zoomMultiple           NOTIFY zoomMultipleChanged)
    Q_PROPERTY(bool     recording           READ recording              NOTIFY configChanged)
    Q_PROPERTY(bool     hdrEnabled          READ hdrEnabled             NOTIFY configChanged)
    Q_PROPERTY(int      motionMode          READ motionMode             NOTIFY configChanged)
    Q_PROPERTY(QString  recordingStatusText READ recordingStatusText    NOTIFY configChanged)

    /// Laser rangefinder distance in metres, NaN when no recent reading. ZT30 only.
    Q_PROPERTY(double   rangefinderDistance READ rangefinderDistance    NOTIFY rangefinderDistanceChanged)
    Q_PROPERTY(bool     rangefinderAvailable READ rangefinderAvailable  NOTIFY rangefinderDistanceChanged)

    /// Hottest and coldest temperature in the thermal image, NaN when unavailable.
    Q_PROPERTY(double   thermalMaxTempC     READ thermalMaxTempC        NOTIFY thermalRangeChanged)
    Q_PROPERTY(double   thermalMinTempC     READ thermalMinTempC        NOTIFY thermalRangeChanged)
    Q_PROPERTY(bool     thermalRangeAvailable READ thermalRangeAvailable NOTIFY thermalRangeChanged)

public:
    explicit SiyiCameraController(QObject *parent = nullptr);
    ~SiyiCameraController();

    static SiyiCameraController *instance();
    static SiyiCameraController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    /// Starts the link if the feature is enabled and keeps it following the settings. Call
    /// once, after SettingsManager::init().
    void init();

    /// Opens the socket and starts polling using the configured address. Safe to call when
    /// already started; it restarts against the current settings.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    /// Slew rates as a percentage of the gimbal maximum, -100..100. The rate is re-sent
    /// periodically until a zero rate stops it.
    Q_INVOKABLE void rotate(int yawRate, int pitchRate);
    Q_INVOKABLE void stopRotation() { rotate(0, 0); }
    Q_INVOKABLE void center();

    Q_INVOKABLE void takePhoto();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void toggleHdr();

    /// SiyiProtocol::MotionMode value.
    Q_INVOKABLE void setMotionMode(int mode);

    /// `direction` is 1 to zoom in, -1 to zoom out, 0 to stop.
    Q_INVOKABLE void zoom(int direction);
    Q_INVOKABLE void setZoom(double multiple);
    Q_INVOKABLE void autoFocus();

    /// SiyiProtocol::CameraImageType value selecting which sensors feed the main and sub
    /// streams. ZT30 only.
    Q_INVOKABLE void setCameraImageType(int imageType);

    /// SiyiProtocol::ThermalPalette / ThermalGain values.
    Q_INVOKABLE void setThermalPalette(int palette);
    Q_INVOKABLE void setThermalGain(int gain);

    [[nodiscard]] bool connected() const { return _connected; }
    [[nodiscard]] QString model() const { return _model; }
    [[nodiscard]] QString firmwareVersion() const { return _firmwareVersion; }
    [[nodiscard]] bool isZT30() const { return _model == QStringLiteral("ZT30"); }

    [[nodiscard]] double yawDeg() const { return _attitude.yawDeg; }
    [[nodiscard]] double pitchDeg() const { return _attitude.pitchDeg; }
    [[nodiscard]] double rollDeg() const { return _attitude.rollDeg; }

    [[nodiscard]] double zoomMultiple() const { return _zoomMultiple; }
    [[nodiscard]] bool recording() const { return _config.recordingStatus == SiyiProtocol::RecordingStatus::On; }
    [[nodiscard]] bool hdrEnabled() const { return _config.hdrEnabled; }
    [[nodiscard]] int motionMode() const { return static_cast<int>(_config.motionMode); }
    [[nodiscard]] QString recordingStatusText() const;

    [[nodiscard]] double rangefinderDistance() const { return _rangefinderDistance; }
    [[nodiscard]] bool rangefinderAvailable() const { return std::isfinite(_rangefinderDistance); }
    [[nodiscard]] double thermalMaxTempC() const { return _thermalMaxTempC; }
    [[nodiscard]] double thermalMinTempC() const { return _thermalMinTempC; }
    [[nodiscard]] bool thermalRangeAvailable() const
    {
        return std::isfinite(_thermalMaxTempC) && std::isfinite(_thermalMinTempC);
    }

signals:
    void connectedChanged();
    void modelChanged();
    void firmwareVersionChanged();
    void attitudeChanged();
    void zoomMultipleChanged();
    void configChanged();
    void rangefinderDistanceChanged();
    void thermalRangeChanged();

    /// Raised for failures the camera reports itself, e.g. a photo that could not be saved.
    void cameraError(const QString &message);

private slots:
    void _readPendingDatagrams();
    void _poll();

private:
    void _send(const QByteArray &packet);
    void _sendCommand(SiyiProtocol::CommandId commandId, const QByteArray &data = QByteArray());
    void _sendSingleByte(SiyiProtocol::CommandId commandId, quint8 value);
    void _handleFrame(const SiyiProtocol::Frame &frame);
    void _handleFunctionFeedback(quint8 code);
    void _resetCameraState();
    void _setConnected(bool connected);

    QUdpSocket *_socket = nullptr;
    QTimer _pollTimer;
    QHostAddress _cameraAddress;
    quint16 _cameraPort = 0;
    QByteArray _rxBuffer;
    quint16 _sequence = 0;

    /// Milliseconds since the last valid frame, used to decide the connected state.
    QElapsedTimer _lastFrameTimer;
    QElapsedTimer _lastRangefinderTimer;
    QElapsedTimer _lastThermalRangeTimer;
    bool _connected = false;
    bool _initialized = false;
    int _pollTicks = 0;

    int _yawRate = 0;
    int _pitchRate = 0;

    QString _model;
    QString _firmwareVersion;
    SiyiProtocol::Attitude _attitude;
    SiyiProtocol::ConfigInfo _config;
    double _zoomMultiple = 1.0;
    double _rangefinderDistance = std::numeric_limits<double>::quiet_NaN();
    double _thermalMaxTempC = std::numeric_limits<double>::quiet_NaN();
    double _thermalMinTempC = std::numeric_limits<double>::quiet_NaN();
};
