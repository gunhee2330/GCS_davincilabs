#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtQmlIntegration/QtQmlIntegration>

#include "SiyiAiProtocol.h"
#include "SiyiProtocol.h"

Q_DECLARE_LOGGING_CATEGORY(SiyiAiControllerLog)

class QQmlEngine;
class QJSEngine;
class QUdpSocket;

/// \brief Drives the SIYI AI tracking module over its own UDP endpoint.
///
/// The module is separate hardware from the optical pod and answers on its own address
/// (192.168.144.60 by default, the pod is .25), so it gets its own socket rather than
/// sharing SiyiCameraController's. It recognises and follows one selected target at a time;
/// the target's box arrives as a coordinate stream and is exposed here normalised to 0..1
/// for overlay drawing.
class SiyiAiController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

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

    [[nodiscard]] int streamWidth() const { return _streamWidth; }
    [[nodiscard]] int streamHeight() const { return _streamHeight; }
    void setStreamWidth(int width);
    void setStreamHeight(int height);

signals:
    void connectedChanged();
    void recognitionEnabledChanged();
    void targetChanged();
    void streamResolutionChanged();

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

    int _streamWidth = SiyiAi::kReferenceWidth;
    int _streamHeight = SiyiAi::kReferenceHeight;
};
