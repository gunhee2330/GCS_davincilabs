#pragma once

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>

class PoliceKcmvpRelay;
class QTimer;

Q_DECLARE_LOGGING_CATEGORY(PoliceKcmvpBridgeLog)

/// \brief Carries the vehicle's MAVLink and the payload's control and video between loopback
/// ports the app uses and the airframe behind the KCMVP encryption module pair (TNGEN MUMT-1000).
///
/// Android sends an app's sockets over its default network, which on the UniRC 7 is Wi-Fi, and
/// nothing in QGC can pick another. The ground module hands the decrypted traffic over on the
/// handset's USB Ethernet adapter only, so this bridge owns the sockets tied to that network and
/// the rest of the app talks to it on 127.0.0.1:
///
///   UDP 14720 -> airframe module MAVLink service, 192.168.50.35:1472
///   UDP 37260 -> ZT30 SDK,         192.168.50.26:37260
///   TCP 8554  -> ZT30 RTSP,        192.168.50.26:8554
///   TCP 8555  -> FPV camera RTSP,  192.168.50.25:8554
///   TCP 8082  -> ZT30 media API,   192.168.50.26:82 (recordings list and download)
///   UDP 37262 -> AI module SDK,    192.168.50.60:37260 (loopback 37260 is the pod's)
///   TCP 37256 -> AI module counts, 192.168.50.60:37256 (the private link detections arrive on)
///   TCP 8556  -> AI module RTSP,   192.168.50.60:554
///
/// The module answers only senders in 192.168.50.x, and the FPV camera cannot be given a gateway,
/// so the payload lives in that network too. With no USB Ethernet adapter the payload routes fall
/// back to the cameras' SIYI-side addresses, so an airframe without the module works through the
/// same settings.
///
/// Android only; elsewhere start() does nothing.
class PoliceKcmvpBridge : public QObject
{
    Q_OBJECT

public:
    /// Loopback ports the station's settings point at.
    static constexpr quint16 LinkPort = 14720;
    static constexpr quint16 PodControlPort = 37260;
    static constexpr quint16 PodVideoPort = 8554;
    static constexpr quint16 FpvVideoPort = 8555;
    static constexpr quint16 PodMediaPort = 8082;
    static constexpr quint16 AiControlPort = 37262;
    static constexpr quint16 AiCountPort = 37256;
    static constexpr quint16 AiVideoPort = 8556;

    explicit PoliceKcmvpBridge(QObject* parent = nullptr);
    ~PoliceKcmvpBridge() override;

    static PoliceKcmvpBridge* instance();

    /// Opens the loopback ports and starts watching for the USB Ethernet network.
    void start();

private:
    void _refreshNetwork();

    QList<PoliceKcmvpRelay*> _relays;
    /// Routes that could not be tied to the current network and fell back to the SIYI side. They
    /// are retried once a test socket ties again, since the cause can clear (a VPN switched off).
    QList<PoliceKcmvpRelay*> _degraded;
    QTimer* _networkTimer = nullptr;
    qint64 _networkHandle = -1;
};
