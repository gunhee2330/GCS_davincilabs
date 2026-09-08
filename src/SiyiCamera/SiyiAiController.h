#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtQmlIntegration/QtQmlIntegration>

#include "SiyiAiProtocol.h"
#include "SiyiLongProtocol.h"
#include "SiyiProtocol.h"

Q_DECLARE_LOGGING_CATEGORY(SiyiAiControllerLog)

class QQmlEngine;
class QJSEngine;
class QTcpSocket;
class QUdpSocket;

/// \brief Drives the SIYI AI tracking module over its own UDP endpoint.
///
/// The module is separate hardware from the optical pod and answers on its own address
/// (192.168.144.60 by default, the pod is .25), so it gets its own socket rather than
/// sharing SiyiCameraController's. It recognises and follows one selected target at a time;
/// the target's box arrives as a coordinate stream and is exposed here normalised to 0..1
/// for overlay drawing.
///
/// It also holds a second socket to the same module's undocumented TCP port, which is the only
/// place object counts come from. That link is optional in every sense: it reconnects on its own,
/// and everything above stays working when the module is absent, which on a bench it always is.
class SiyiAiController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    friend class SiyiAiControllerTest;

    Q_PROPERTY(bool     connected           READ connected          NOTIFY connectedChanged)
    Q_PROPERTY(bool     recognitionEnabled  READ recognitionEnabled NOTIFY recognitionEnabledChanged)

    /// True while a target box arrived recently. The box coordinates below are only
    /// meaningful while this holds.
    Q_PROPERTY(bool     hasTarget           READ hasTarget          NOTIFY targetChanged)
    Q_PROPERTY(double   targetCentreX       READ targetCentreX      NOTIFY targetChanged)
    Q_PROPERTY(double   targetCentreY       READ targetCentreY      NOTIFY targetChanged)
    Q_PROPERTY(double   targetWidth         READ targetWidth        NOTIFY targetChanged)
    Q_PROPERTY(double   targetHeight        READ targetHeight       NOTIFY targetChanged)
    Q_PROPERTY(QString  targetTypeName      READ targetTypeName     NOTIFY targetChanged)
    Q_PROPERTY(bool     targetLost          READ targetLost         NOTIFY targetChanged)

    /// Resolution the module's video is delivered at. Target selection coordinates are sent
    /// in this space per the SDK, while the reported target stream always uses the module's
    /// fixed reference frame, so the two are kept separate.
    Q_PROPERTY(int      streamWidth         READ streamWidth        WRITE setStreamWidth    NOTIFY streamResolutionChanged)
    Q_PROPERTY(int      streamHeight        READ streamHeight       WRITE setStreamHeight   NOTIFY streamResolutionChanged)

    /// The module refused to switch recognition on because its input video is larger than
    /// 1920x1080. Without this the refusal is silent and looks like a dead link.
    Q_PROPERTY(bool     streamTooLarge      READ streamTooLarge     NOTIFY streamTooLargeChanged)

    /// Counts of everything the module currently sees, by class, from its private link. The
    /// counts below only mean anything while this holds: it goes false when the link drops, when
    /// counting is off, and when the numbers stop arriving.
    Q_PROPERTY(bool     countsValid             READ countsValid            NOTIFY countsChanged)

    /// -1 means unknown, and must be drawn as a dash rather than a number. The class names belong
    /// to whichever model the module has loaded and nobody has yet read one on real hardware; a
    /// model that calls people "pedestrian" leaves nothing to sum, and the empty sum is zero,
    /// which reads as "none in view" from the middle of a crowd. Person and vehicle are separate
    /// because a model with no vehicle class is ordinary and one with no person class is not.
    Q_PROPERTY(int      personCount             READ personCount            NOTIFY countsChanged)
    Q_PROPERTY(int      vehicleCount            READ vehicleCount           NOTIFY countsChanged)

    /// The count above is a floor, not a total: a class tally reached 255, the largest number the
    /// module's own unsigned byte holds. Show it as "255+".
    ///
    /// This flag does not catch wraparound, and cannot. The firmware accumulates into that byte
    /// and lets it wrap (SiyiAi::kObjectCountSaturation), so a crowd of 300 arrives as 44 with
    /// nothing to distinguish it from 44 people. The flag only fires while the wire value sits
    /// exactly on 255. Treat every count here as a lower bound of unmeasured tightness until the
    /// module's per-frame object limit has been measured on a bench.
    Q_PROPERTY(bool     personCountSaturated    READ personCountSaturated   NOTIFY countsChanged)
    Q_PROPERTY(bool     vehicleCountSaturated   READ vehicleCountSaturated  NOTIFY countsChanged)

public:
    /// No default argument: a default-constructible QML_SINGLETON is default-constructed by the
    /// engine instead of going through create(), which hands QML a second, inert instance while
    /// the real one talks to the hardware.
    explicit SiyiAiController(QObject *parent);
    ~SiyiAiController();

    static SiyiAiController *instance();
    static SiyiAiController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    /// Starts the link if the feature is enabled and keeps it following the settings. Call
    /// once, after SettingsManager::init().
    void init();

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    Q_INVOKABLE void setRecognition(bool enabled);

    /// Picks the object under a tap. Coordinates are normalised 0..1 across the video frame;
    /// they are scaled to the module's reference resolution before sending.
    Q_INVOKABLE void trackPoint(double x, double y);

    /// Selects a target by dragging a box, normalised 0..1.
    Q_INVOKABLE void trackBox(double left, double top, double right, double bottom);

    Q_INVOKABLE void cancelTracking();

    [[nodiscard]] bool connected() const { return _connected; }
    [[nodiscard]] bool recognitionEnabled() const { return _recognitionEnabled; }
    [[nodiscard]] bool hasTarget() const { return _hasTarget; }
    [[nodiscard]] double targetCentreX() const { return static_cast<double>(_target.centreX) / SiyiAi::kReferenceWidth; }
    [[nodiscard]] double targetCentreY() const { return static_cast<double>(_target.centreY) / SiyiAi::kReferenceHeight; }
    [[nodiscard]] double targetWidth() const { return static_cast<double>(_target.width) / SiyiAi::kReferenceWidth; }
    [[nodiscard]] double targetHeight() const { return static_cast<double>(_target.height) / SiyiAi::kReferenceHeight; }
    [[nodiscard]] QString targetTypeName() const { return SiyiAi::targetTypeName(_target.type); }
    [[nodiscard]] bool targetLost() const { return _target.status == SiyiAi::TrackingStatus::Lost; }

    [[nodiscard]] bool streamTooLarge() const { return _streamTooLarge; }
    [[nodiscard]] bool countsValid() const { return _countsValid; }

    /// -1 when the module's class list names nothing of this kind; see the properties above.
    [[nodiscard]] int personCount() const { return _personCount; }
    [[nodiscard]] int vehicleCount() const { return _vehicleCount; }
    [[nodiscard]] bool personCountSaturated() const { return _personSaturated; }
    [[nodiscard]] bool vehicleCountSaturated() const { return _vehicleSaturated; }

    [[nodiscard]] int streamWidth() const { return _streamWidth; }
    [[nodiscard]] int streamHeight() const { return _streamHeight; }
    void setStreamWidth(int width);
    void setStreamHeight(int height);

signals:
    void connectedChanged();
    void recognitionEnabledChanged();
    void targetChanged();
    void streamResolutionChanged();
    void streamTooLargeChanged();
    void countsChanged();

    /// Raised when the module refuses a track request, with a user readable reason.
    void trackRequestFailed(const QString &reason);

private slots:
    void _readPendingDatagrams();
    void _poll();

private:
    void _send(const QByteArray &packet);
    void _handleFrame(const SiyiProtocol::Frame &frame);
    void _setConnected(bool connected);
    void _setHasTarget(bool hasTarget);
    void _setStreamTooLarge(bool tooLarge);

    void _sendCount(SiyiAi::PrivateCommandId command, const QByteArray &payload = QByteArray());
    void _readCountLink();
    void _countLinkOpened();
    void _handleCountFrame(const SiyiLongProtocol::Frame &frame);
    void _applyCounts(const QList<int> &counts);
    void _dropClassMapping();
    void _setCountsValid(bool valid);

    QUdpSocket *_socket = nullptr;
    QTimer _pollTimer;
    QHostAddress _moduleAddress;
    quint16 _modulePort = 0;
    QByteArray _rxBuffer;
    quint16 _sequence = 0;

    QElapsedTimer _lastFrameTimer;
    QElapsedTimer _lastTargetTimer;
    bool _connected = false;
    bool _recognitionEnabled = false;
    bool _hasTarget = false;
    bool _streamRequested = false;
    int _pollTicks = 0;

    SiyiAi::TrackedTarget _target;

    QTcpSocket *_countSocket = nullptr;
    QByteArray _countRxBuffer;
    quint16 _countSequence = 0;
    QElapsedTimer _lastCountTimer;

    /// The module's class list, in its own order, and the slots within it that count as people
    /// and as vehicles. Read from the module rather than assumed: the numbering belongs to
    /// whichever model it has loaded.
    QStringList _classNames;
    QList<int> _personClasses;
    QList<int> _vehicleClasses;

    /// Which model the list above was read from, as the module numbered it, or -1 before any
    /// list has been read. Every 0xD5 reply and push carries the same byte (spec section 4), so
    /// it is what says a tally still belongs to this mapping.
    int _classModelId = -1;

    /// Set while the ack to a counting-on write is still to come. That ack repeats the class
    /// count as a row of zeroes, which is a valid empty tally on the wire; see _handleCountFrame().
    bool _countStartAckPending = false;

    /// Set when the module last said counting was off. The write that switches it back on is due
    /// on the next keep-alive tick rather than on the report itself, which is what keeps a module
    /// that refuses to start from turning this into a request/reply loop; see _handleCountFrame().
    bool _countStartWanted = false;

    int _personCount = -1;
    int _vehicleCount = -1;
    bool _personSaturated = false;
    bool _vehicleSaturated = false;
    bool _countsValid = false;

    bool _streamTooLarge = false;

    /// A cancel waiting on the module's tracking state, the frame sequence of the query it is
    /// waiting on, and how long it has waited; see cancelTracking() and _poll().
    bool _cancelPending = false;
    quint16 _cancelSequence = 0;
    QElapsedTimer _cancelTimer;

    int _streamWidth = SiyiAi::kReferenceWidth;
    int _streamHeight = SiyiAi::kReferenceHeight;
};
