#pragma once

#include <cmath>
#include <limits>

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtPositioning/QGeoCoordinate>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QString>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtQmlIntegration/QtQmlIntegration>

#include "SiyiProtocol.h"

Q_DECLARE_LOGGING_CATEGORY(SiyiCameraControllerLog)

class MissionAutoRecord;
class QQmlEngine;
class QJSEngine;
class QUdpSocket;
class Vehicle;

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
    /// The pod answering that it has nowhere to put a photo or a video. Both commands are
    /// refused in that state and the pod says nothing further about it, so a control that
    /// offers them anyway is one that does nothing when pressed.
    Q_PROPERTY(bool     noSdCard            READ noSdCard               NOTIFY configChanged)

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
    /// The operator's choice for the laser, kept for the session and re-applied on every pod
    /// connect. On by default. Off stops the controller from ever lighting it.
    Q_PROPERTY(bool laserEnabled READ laserEnabled WRITE setLaserEnabled NOTIFY laserStateChanged)

    /// Hottest and coldest temperature in the thermal image, NaN when unavailable.
    Q_PROPERTY(double   thermalMaxTempC     READ thermalMaxTempC        NOTIFY thermalRangeChanged)
    Q_PROPERTY(double   thermalMinTempC     READ thermalMinTempC        NOTIFY thermalRangeChanged)
    Q_PROPERTY(bool     thermalRangeAvailable READ thermalRangeAvailable NOTIFY thermalRangeChanged)

    /// 0 for the low gain band and 1 for the high one, or -1 before the pod has said.
    ///
    /// Which band is loaded decides what the camera can read at all: high covers -20 to 150 C
    /// and low covers 50 to 550 C, so a fire seen on the high band saturates and reads 150.
    Q_PROPERTY(int      thermalGain         READ thermalGain            NOTIFY thermalGainChanged)
    Q_PROPERTY(QString  thermalGainRangeText READ thermalGainRangeText  NOTIFY thermalGainChanged)

    /// True while the pod reports it is correcting readings for emissivity and for the air.
    Q_PROPERTY(bool     thermalCorrectionOn READ thermalCorrectionOn    NOTIFY thermalCalibrationChanged)
    /// Distance last sent to the pod as the correction's path length, in metres.
    Q_PROPERTY(double   thermalCalibrationDistanceM READ thermalCalibrationDistanceM NOTIFY thermalCalibrationChanged)

    /// A point of the thermal picture the operator asked to read, and what came back. Active
    /// says a point is set, available says a reading has arrived for it, and noReply says the
    /// pod ignored the request - firmware that predates the point command does exactly that.
    Q_PROPERTY(bool     pointTemperatureActive    READ pointTemperatureActive    NOTIFY pointTemperatureChanged)
    Q_PROPERTY(bool     pointTemperatureAvailable READ pointTemperatureAvailable NOTIFY pointTemperatureChanged)
    Q_PROPERTY(bool     pointTemperatureNoReply   READ pointTemperatureNoReply   NOTIFY pointTemperatureChanged)
    Q_PROPERTY(double   pointTemperatureC         READ pointTemperatureC         NOTIFY pointTemperatureChanged)
    Q_PROPERTY(QPointF  pointTemperaturePoint     READ pointTemperaturePoint     NOTIFY pointTemperatureChanged)

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

    /// A handset button asking the fly view to swap which picture fills the screen, EO or IR.
    /// Nothing goes to the pod: this class only relays it, because it is where the handset's
    /// pod buttons already arrive and the screen already listens to it. The fly view decides
    /// what "swap" means for the windows it has up.
    Q_INVOKABLE void requestEoIrViewToggle();

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
    /// Swaps to the other gain band, so one button covers both.
    Q_INVOKABLE void toggleThermalGain();

    /// Reads the temperature under a point of the thermal picture, given as fractions across
    /// and down it, and keeps reading it until stopPointTemperature().
    Q_INVOKABLE void measurePointTemperature(double x, double y);
    Q_INVOKABLE void stopPointTemperature();

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
    [[nodiscard]] bool noSdCard() const {
        return _config.recordingStatus == SiyiProtocol::RecordingStatus::NoCard;
    }

    [[nodiscard]] double rangefinderDistance() const { return _rangefinderDistance; }
    [[nodiscard]] bool rangefinderAvailable() const { return std::isfinite(_rangefinderDistance); }
    [[nodiscard]] QGeoCoordinate rangefinderTarget() const { return _rangefinderTarget; }
    [[nodiscard]] bool rangefinderTargetAvailable() const { return _rangefinderTarget.isValid(); }
    [[nodiscard]] bool laserOn() const { return _laserOn; }
    [[nodiscard]] bool laserEnabled() const { return _laserEnabled; }
    void setLaserEnabled(bool on);
    [[nodiscard]] bool aiFollowEnabled() const { return _aiFollowEnabled; }
    [[nodiscard]] int aiFollowError() const { return static_cast<int>(_aiFollowError); }
    [[nodiscard]] bool aiFollowStale() const { return _aiFollowStale; }
    [[nodiscard]] int aiFollowStopState() const { return static_cast<int>(_aiFollowStopState); }
    [[nodiscard]] int thermalGain() const { return _thermalGain; }
    [[nodiscard]] QString thermalGainRangeText() const;
    [[nodiscard]] bool thermalCorrectionOn() const { return _thermalCorrectionOn; }
    [[nodiscard]] double thermalCalibrationDistanceM() const { return _sentCalibration.distanceM; }

    [[nodiscard]] double thermalMaxTempC() const { return _thermalMaxTempC; }
    [[nodiscard]] double thermalMinTempC() const { return _thermalMinTempC; }
    [[nodiscard]] bool pointTemperatureActive() const { return _pointActive; }
    [[nodiscard]] bool pointTemperatureAvailable() const { return _pointActive && std::isfinite(_pointTempC); }
    [[nodiscard]] bool pointTemperatureNoReply() const { return _pointNoReply; }
    [[nodiscard]] double pointTemperatureC() const { return _pointTempC; }
    [[nodiscard]] QPointF pointTemperaturePoint() const { return _pointPoint; }
    [[nodiscard]] bool thermalRangeAvailable() const
    {
        return std::isfinite(_thermalMaxTempC) && std::isfinite(_thermalMinTempC);
    }

signals:
    void connectedChanged();
    /// The pod answered again after the link timed out. Once per return, never on first contact
    /// and never after stop() (a settings edit restarting the link is not a loss).
    void podReconnected();
    void modelChanged();
    void firmwareVersionChanged();
    void attitudeChanged();
    void zoomMultipleChanged();
    void cameraImageTypeChanged();
    void configChanged();
    void rangefinderDistanceChanged();
    void rangefinderTargetChanged();
    void laserStateChanged();
    void eoIrViewToggleRequested();
    /// Asks the dashboard to make \a panel ("primary", "secondary", "shared") the big picture;
    /// "" gives the map back. Raised by the handset keys (PoliceRcKeys).
    void mainPictureRequested(QString panel);
    void thermalRangeChanged();
    void thermalGainChanged();
    void thermalCalibrationChanged();
    void pointTemperatureChanged();
    void aiFollowChanged();

    /// Raised for failures the camera reports itself, e.g. a photo that could not be saved.
    void cameraError(const QString &message);

private slots:
    void _readPendingDatagrams();
    void _poll();
    void _activeVehicleChanged(Vehicle *vehicle);

private:
    /// False when nothing left this process - no socket, or the write failed. The stop path needs
    /// the difference: a stop that was never sent is not a stop that is waiting for an answer.
    bool _send(const QByteArray &packet);
    void _sendCommand(SiyiProtocol::CommandId commandId, const QByteArray &data = QByteArray());
    bool _sendSingleByte(SiyiProtocol::CommandId commandId, quint8 value);
    void _handleFrame(const SiyiProtocol::Frame &frame);
    void _handleFunctionFeedback(quint8 code);
    void _resetCameraState();

    /// Pushes the aircraft's attitude and position to the pod, which has neither of its own and
    /// cannot geolocate its laser spot without them.
    void _sendFcData();

    /// Writes the correction constants when they have moved since the last write. The path
    /// length comes from the rangefinder, so it changes on its own as the aircraft flies.
    void _sendThermalCalibrationIfChanged();
    void _setConnected(bool connected);
    /// toggleRecording() without marking the recording as the operator's
    void _sendRecordingToggle();

    QUdpSocket *_socket = nullptr;
    QTimer _pollTimer;
    QHostAddress _cameraAddress;
    quint16 _cameraPort = 0;
    QByteArray _rxBuffer;
    quint16 _sequence = 0;

    /// Milliseconds since the last valid frame, used to decide the connected state.
    /// Milliseconds since this controller started, which is what the pod's position and
    /// attitude commands want as their boot timestamp. The pod only uses it to order frames,
    /// so a station clock rather than the autopilot's is fine.
    QElapsedTimer _uptime;

    QElapsedTimer _lastFrameTimer;
    QElapsedTimer _lastRangefinderTimer;
    QElapsedTimer _lastRangefinderTargetTimer;
    QElapsedTimer _lastThermalRangeTimer;
    bool _connected = false;
    bool _linkLost = false;
    bool _initialized = false;
    MissionAutoRecord *_autoRecord = nullptr;
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
    /// True once setCameraImageType has sent a routing; until then _cameraImageType is only a
    /// default and the pod is not pulled towards it.
    bool _cameraImageTypeSent = false;
    /// Routing the pod last reported in a 0x10 reply or 0x11 ack, -1 while unknown.
    int _reportedImageType = -1;
    /// Poll tick of the last corrective 0x11, so a mismatch is re-sent at most once per tick.
    int _imageTypeResendTick = -1;
    double _rangefinderDistance = std::numeric_limits<double>::quiet_NaN();
    QGeoCoordinate _rangefinderTarget;
    bool _laserOn = false;
    bool _laserEnabled = true;

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
    /// -1 until the pod answers, so the button can say "unknown" rather than guess a band.
    int _thermalGain = -1;
    bool _thermalCorrectionOn = false;

    /// What was last put on the wire, so the periodic refresh only sends on a real change.
    SiyiProtocol::ThermalCalibration _sentCalibration;
    bool _calibrationSent = false;

    /// Follows the active vehicle only to relay its position and attitude to the pod.
    QPointer<Vehicle> _vehicle;

    double _thermalMaxTempC = std::numeric_limits<double>::quiet_NaN();

    bool _pointActive = false;
    bool _pointNoReply = false;
    double _pointTempC = std::numeric_limits<double>::quiet_NaN();
    QPointF _pointPoint;
    /// Gives the pod its chance to answer a point request before the reading is called missing.
    QTimer _pointReplyTimer;
    double _thermalMinTempC = std::numeric_limits<double>::quiet_NaN();

    friend class SiyiCameraControllerTest;
};
