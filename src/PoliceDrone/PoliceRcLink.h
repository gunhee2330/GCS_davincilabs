#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

class QQmlEngine;
class QJSEngine;

/// \brief The UniRC 7 Pro handset's own link strength, read from SIYI's remote-control service.
///
/// The top bar's RC bars used to come from the flight controller's rssi, which says nothing about
/// the handset's radio. On the handset, PoliceRcLinkMonitor.java binds the service UniGCS reads and
/// feeds LinkInfo here. Anywhere else nothing arrives, available stays false and QML keeps the
/// FC rssi.
class PoliceRcLink : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available    READ available    NOTIFY changed)
    Q_PROPERTY(bool connected    READ connected    NOTIFY changed)
    Q_PROPERTY(int  strength     READ strength     NOTIFY changed)
    Q_PROPERTY(int  quality      READ quality      NOTIFY changed)
    Q_PROPERTY(int  validPercent READ validPercent NOTIFY changed)
    Q_PROPERTY(int  percent      READ percent      NOTIFY changed)

public:
    /// No default argument, for the reason ControllerBattery gives.
    explicit PoliceRcLink(QObject* parent);
    ~PoliceRcLink() override;

    static PoliceRcLink* instance();
    static PoliceRcLink* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// Registers the natives and starts the Java monitor. Android only; call once from QGCApplication.
    void init();

    static constexpr int kStaleMs = 3000;

    /// 0..100 from the raw strength, or -1 for no reading.
    static int percentFromStrength(int raw);

    /// A LinkInfo with a strength arrived within the last kStaleMs.
    [[nodiscard]] bool available() const { return _available; }
    /// The air unit is connected. Edge-reported only, so unknown counts as connected (as UniGCS).
    [[nodiscard]] bool connected() const { return _connected; }
    [[nodiscard]] int strength() const { return _strength; }
    [[nodiscard]] int quality() const { return _quality; }
    [[nodiscard]] int validPercent() const { return _validPercent; }
    [[nodiscard]] int percent() const { return percentFromStrength(_strength); }

public slots:
    void handleLinkInfo(int strength, int quality, int validPercent);
    void handleConnected(bool connected);

signals:
    void changed();

private:
    QTimer _stale;
    bool _available = false;
    bool _connected = true;
    int _strength = -1;
    int _quality = -1;
    int _validPercent = -1;
};
