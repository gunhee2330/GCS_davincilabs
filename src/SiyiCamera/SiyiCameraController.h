#pragma once

#include <cmath>
#include <limits>

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtPositioning/QGeoCoordinate>
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
    Q_PROPERTY(int      cameraImageType     READ cameraImageType        NOTIFY cameraImageTypeChanged)
    Q_PROPERTY(bool     recording           READ recording              NOTIFY configChanged)
    Q_PROPERTY(bool     hdrEnabled          READ hdrEnabled             NOTIFY configChanged)
    Q_PROPERTY(int      motionMode          READ motionMode             NOTIFY configChanged)
    Q_PROPERTY(QString  recordingStatusText READ recordingStatusText    NOTIFY configChanged)

    /// Laser rangefinder distance in metres, NaN when no recent reading. ZT30 only.
    Q_PROPERTY(double   rangefinderDistance READ rangefinderDistance    NOTIFY rangefinderDistanceChanged)
    Q_PROPERTY(bool     rangefinderAvailable READ rangefinderAvailable  NOTIFY rangefinderDistanceChanged)

    /// Where the laser is pointing. Diagnostic only: this is the laser's aim point, not the
    /// tracker's target, and it carries no timestamp of its own, so it must be checked against a
    /// surveyed point before anything flies on it. ZT30 only.
    Q_PROPERTY(QGeoCoordinate rangefinderTarget READ rangefinderTarget NOTIFY rangefinderTargetChanged)
    Q_PROPERTY(bool rangefinderTargetAvailable  READ rangefinderTargetAvailable NOTIFY rangefinderTargetChanged)

    /// True while the pod reports its laser lit. It powers up unlit, so the controller asks for
    /// it once the pod answers; until then range and target coordinate both read as absent.
    /// Reads false on a pod that does not answer 0x31, whose laser may still be lit.
    Q_PROPERTY(bool laserOn READ laserOn NOTIFY laserStateChanged)

    /// Hottest and coldest temperature in the thermal image, NaN when unavailable.
    Q_PROPERTY(double   thermalMaxTempC     READ thermalMaxTempC        NOTIFY thermalRangeChanged)
    Q_PROPERTY(double   thermalMinTempC     READ thermalMinTempC        NOTIFY thermalRangeChanged)
    Q_PROPERTY(bool     thermalRangeAvailable READ thermalRangeAvailable NOTIFY thermalRangeChanged)

    /// True only once the gimbal has confirmed it is following the aircraft. A gimbal whose
    /// firmware predates the command never answers, and reporting follow from the send alone
    /// would tell the operator the pod is tracking when it is not.
    Q_PROPERTY(bool     aiFollowEnabled     READ aiFollowEnabled        NOTIFY aiFollowChanged)

    /// AiFollowError value carrying the gimbal's own refusal reason. Rendered in Korean by the
    /// PoliceDrone QML; no message is built here.
    Q_PROPERTY(int      aiFollowError       READ aiFollowError          NOTIFY aiFollowChanged)

    /// True whenever aiFollowEnabled is not backed by a recent answer - before the first one ever
    /// arrives, which is why it starts true, and again from the moment a new request goes out
    /// until that request is answered.
    /// 0xC3 is answered only when it is asked, so aiFollowEnabled is a memory of a past answer,
    /// not a live state, and nothing can refresh it: a re-query is a re-assert, which would switch
    /// follow back on. Whether the gimbal drops follow by itself - and whether it would send
    /// anything if it did - is unmeasured. Show "unconfirmed" rather than "following" or "off"
    /// while this holds.
    Q_PROPERTY(bool     aiFollowStale       READ aiFollowStale          NOTIFY aiFollowChanged)

    /// AiFollowStop value: what became of the last stop this side asked for. setAiFollow(false) is
    /// one datagram on a link with no retransmission, and it is the command that hands the sticks
    /// back, so what happened to it has to reach the screen. It is not "still following":
    /// aiFollowEnabled goes false on the ask, because from here on nothing is being asked for.
    Q_PROPERTY(int      aiFollowStopState   READ aiFollowStopState       NOTIFY aiFollowChanged)

public:
    /// No default argument: a default-constructible QML_SINGLETON is default-constructed by the
    /// engine instead of going through create(), which hands QML a second, inert instance while
    /// the real one talks to the hardware.
    explicit SiyiCameraController(QObject *parent);
    ~SiyiCameraController();

    /// Reply byte of SiyiProtocol::CommandId::AiFollow. Values are the wire codes: the gimbal
    /// vets GPS, AI state, target selection, altitude and range itself and answers with the
    /// reason it refused, so none of that is re-checked on this side. Named after UniGCS
    /// 3.1.6's n5.f2 (viewmodels/d7.java); the command is undocumented, so a later firmware may
    /// add codes, which arrive here as Unknown.
    enum class AiFollowError {
        None                     = 0,   ///< Not following, with nothing to report.
        TargetTooFarOrLow        = 2,
        AiTrackingDisabled       = 3,
        GpsDataMissing           = 4,
        TargetTooCloseOrHigh     = 5,
        InvertedModeUnsupported  = 6,
        TargetNotSelected        = 7,
        ModelUnsupported         = 8,
        Unknown                  = 255, ///< Not a wire code: any refusal this build cannot name.
    };
    Q_ENUM(AiFollowError)

    /// What became of the last stop asked for. Three outcomes rather than one "pending" flag,
    /// because "sent, no answer yet", "every repeat spent with no answer" and "nothing left the
    /// socket at all" need three different sentences: the first is worth waiting through, the
    /// other two are not, and the last one means the gimbal was never told anything. Values are
    /// prefixed because Q_ENUM names share one scope in QML and AiFollowError already owns None.
    enum class AiFollowStop {
        StopIdle        = 0,   ///< Nothing outstanding: none asked for, or the gimbal answered one.
        StopPending,           ///< Sent, repeats still owed, nothing back yet.
        StopUnconfirmed,       ///< Every repeat spent, or the link torn down, with nothing back.
        StopUnsent,            ///< No datagram went out at all; the socket was gone.
    };
    Q_ENUM(AiFollowStop)

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

    /// The routing last commanded through setCameraImageType. The pod does not report it
    /// back, so this is what we asked for rather than what it is doing; it is still the one
    /// place that knows, which keeps the fly view's label and the joystick toggle agreeing.
    [[nodiscard]] int cameraImageType() const { return _cameraImageType; }

    /// SiyiProtocol::ThermalPalette / ThermalGain values.
    Q_INVOKABLE void setThermalPalette(int palette);
    Q_INVOKABLE void setThermalGain(int gain);

    /// Asks the gimbal to point itself at the aircraft's AI target, or to stop. The state it
    /// reports back lands in aiFollowEnabled / aiFollowError.
    Q_INVOKABLE void setAiFollow(bool on);

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
    [[nodiscard]] QGeoCoordinate rangefinderTarget() const { return _rangefinderTarget; }
    [[nodiscard]] bool rangefinderTargetAvailable() const { return _rangefinderTarget.isValid(); }
    [[nodiscard]] bool laserOn() const { return _laserOn; }
    [[nodiscard]] bool aiFollowEnabled() const { return _aiFollowEnabled; }
    [[nodiscard]] int aiFollowError() const { return static_cast<int>(_aiFollowError); }
    [[nodiscard]] bool aiFollowStale() const { return _aiFollowStale; }
    [[nodiscard]] int aiFollowStopState() const { return static_cast<int>(_aiFollowStopState); }
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
    void cameraImageTypeChanged();
    void configChanged();
    void rangefinderDistanceChanged();
    void rangefinderTargetChanged();
    void laserStateChanged();
    void thermalRangeChanged();
    void aiFollowChanged();

    /// Raised for failures the camera reports itself, e.g. a photo that could not be saved.
    void cameraError(const QString &message);

private slots:
    void _readPendingDatagrams();
    void _poll();

private:
    /// False when nothing left this process - no socket, or the write failed. The stop path needs
    /// the difference: a stop that was never sent is not a stop that is waiting for an answer.
    bool _send(const QByteArray &packet);
    void _sendCommand(SiyiProtocol::CommandId commandId, const QByteArray &data = QByteArray());
    bool _sendSingleByte(SiyiProtocol::CommandId commandId, quint8 value);
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
    QElapsedTimer _lastRangefinderTargetTimer;
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
    /// Sensor routing as last commanded; the pod sends no readback.
    int _cameraImageType = static_cast<int>(SiyiProtocol::CameraImageType::MainZoomSubThermal);
    double _rangefinderDistance = std::numeric_limits<double>::quiet_NaN();
    QGeoCoordinate _rangefinderTarget;
    bool _laserOn = false;

    /// Laser on-commands the poll may still send on this link. Primed by _resetCameraState(),
    /// spent as soon as the pod confirms the laser lit.
    int _laserOnAttemptsLeft = 0;
    bool _aiFollowEnabled = false;

    /// The last value setAiFollow() asked for. No confirmed way to tell which request a 0xC3 reply
    /// answers has been recovered (spec section 8 leaves it unmeasured), so matching against what
    /// was last asked for is what is left; see setAiFollow().
    bool _aiFollowRequested = false;
    AiFollowError _aiFollowError = AiFollowError::None;
    /// Starts true: before the first 0xC3 reply this side has never observed follow at all, and
    /// the gimbal keeps following across a GCS restart or a follow the hand controller started.
    /// False here would paint a confident "off" over an aircraft that is chasing a person.
    bool _aiFollowStale = true;
    QElapsedTimer _lastAiFollowTimer;

    /// Repeats of 0xC3{0} the poll still owes, and the tick the next one is due on. A stop is the
    /// only way back to the sticks and it travels on the same unacknowledged link as everything
    /// else here, so it is sent more than once. Bounded rather than repeated until confirmed: the
    /// reply to a stop is not in the recovered spec (section 6 documents 1 and 2..8 only), so a
    /// gimbal that answers a stop with 1 would keep an unbounded loop running for the flight.
    int _aiFollowStopSendsLeft = 0;
    int _aiFollowStopNextTick = 0;
    AiFollowStop _aiFollowStopState = AiFollowStop::StopIdle;
    double _thermalMaxTempC = std::numeric_limits<double>::quiet_NaN();
    double _thermalMinTempC = std::numeric_limits<double>::quiet_NaN();

    friend class SiyiCameraControllerTest;
};
