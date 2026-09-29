#include "PoliceKcmvpBridge.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QTimer>

#ifdef Q_OS_ANDROID
#include <QtCore/QCoreApplication>
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#include <QtCore/QSocketNotifier>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>

#include <android/multinetwork.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(PoliceKcmvpBridgeLog, "PoliceDrone.KcmvpBridge")

/// One loopback port and where its traffic goes.
class PoliceKcmvpRelay : public QObject
{
public:
    using QObject::QObject;

    /// Opens the loopback side. False if the port is taken.
    virtual bool open() = 0;

    /// Sends new traffic through the USB Ethernet network with this handle, or to the SIYI side
    /// when it is 0. False if a socket could not be tied to the network.
    virtual bool setNetwork(qint64 networkHandle) = 0;
};

namespace {

#ifdef Q_OS_ANDROID
struct Route
{
    const char* name;
    quint16 localPort;
    /// Behind the airframe module, reached through the USB Ethernet adapter.
    const char* kcmvpHost;
    /// On the SIYI side, used when there is no USB Ethernet adapter; nullptr for none.
    const char* directHost;
    quint16 remotePort;
};

/// The airframe module's MAVLink service at its factory address (MUMT-1000 manual 7.5). It
/// answers only senders in its own 192.168.50.0/24.
constexpr Route kMavlinkRoute{"MAVLink", PoliceKcmvpBridge::LinkPort, "192.168.50.35", nullptr, 1472};
constexpr Route kPodControlRoute{"ZT30 SDK", PoliceKcmvpBridge::PodControlPort, "192.168.50.26", "192.168.144.26",
                                 37260};
constexpr Route kPodVideoRoute{"ZT30 RTSP", PoliceKcmvpBridge::PodVideoPort, "192.168.50.26", "192.168.144.26", 8554};
constexpr Route kFpvVideoRoute{"FPV RTSP", PoliceKcmvpBridge::FpvVideoPort, "192.168.50.25", "192.168.144.25", 8554};
/// Plain HTTP, which passes the RTSP header rewrite untouched: that only fires on "RTSP/" replies.
constexpr Route kPodMediaRoute{"ZT30 media", PoliceKcmvpBridge::PodMediaPort, "192.168.50.26", "192.168.144.26", 82};
/// The AI tracking module reads the pod's picture, so it sits beside the pod behind the airframe
/// module: its setip.txt moves it to 50.60 and points it at the pod's 50.26. 144.60 is its
/// factory address on the SIYI side.
constexpr Route kAiControlRoute{"AI SDK", PoliceKcmvpBridge::AiControlPort, "192.168.50.60", "192.168.144.60", 37260};
constexpr Route kAiCountRoute{"AI counts", PoliceKcmvpBridge::AiCountPort, "192.168.50.60", "192.168.144.60", 37256};
constexpr Route kAiVideoRoute{"AI RTSP", PoliceKcmvpBridge::AiVideoPort, "192.168.50.60", "192.168.144.60", 554};

/// RTSP's own port, which a server may leave out of the addresses it hands back.
constexpr quint16 kRtspDefaultPort = 554;

/// The handset's built-in port faces the SIYI radio, the ciphertext side. The USB adapter is
/// the other ethernet interface.
constexpr const char* kBuiltInInterface = "eth0";

/// Polled because a NetworkCallback needs a Java class of our own. The adapter comes and goes
/// with its cable, and a replug hands out a new network, so the sockets have to follow.
constexpr int kNetworkPollMsecs = 3000;

constexpr int kConnectTimeoutMsecs = 5000;

/// Short enough that the SIYI-side fallback still answers inside rtspsrc's 5 s TCP timeout.
constexpr int kKcmvpAttemptMsecs = 2000;

/// Handle of the USB Ethernet adapter's network, or 0 while it has none.
qint64 findUsbEthernetNetwork()
{
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject service = QJniObject::fromString(QStringLiteral("connectivity"));
    const QJniObject manager = context.callObjectMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                                        service.object<jstring>());
    const QJniObject networks =
        manager.isValid() ? manager.callObjectMethod("getAllNetworks", "()[Landroid/net/Network;") : QJniObject();
    QJniEnvironment env;
    if (env.checkAndClearExceptions() || !networks.isValid()) {
        return 0;
    }

    const auto array = networks.object<jobjectArray>();
    const jsize count = env->GetArrayLength(array);
    for (jsize i = 0; i < count; ++i) {
        const QJniObject network = QJniObject::fromLocalRef(env->GetObjectArrayElement(array, i));
        const QJniObject properties = manager.callObjectMethod(
            "getLinkProperties", "(Landroid/net/Network;)Landroid/net/LinkProperties;", network.object());
        if (env.checkAndClearExceptions() || !properties.isValid()) {
            continue;
        }
        const QString name = properties.callObjectMethod<jstring>("getInterfaceName").toString();
        if (name.startsWith(QLatin1String("eth")) && (name != QLatin1String(kBuiltInInterface))) {
            const jlong handle = network.callMethod<jlong>("getNetworkHandle");
            return env.checkAndClearExceptions() ? 0 : static_cast<qint64>(handle);
        }
    }
    return 0;
}

bool tieToNetwork(qintptr fd, qint64 networkHandle)
{
    if (android_setsocknetwork(static_cast<net_handle_t>(networkHandle), static_cast<int>(fd)) == 0) {
        return true;
    }
    qCWarning(PoliceKcmvpBridgeLog) << "could not tie a socket to the USB Ethernet network:" << std::strerror(errno);
    return false;
}

/// Whether a socket can be tied to the network now, checked quietly so a failed route can be
/// retried without a warning every poll.
bool canTieToNetwork(qint64 networkHandle)
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }
    const bool tied = (android_setsocknetwork(static_cast<net_handle_t>(networkHandle), fd) == 0);
    (void) ::close(fd);
    return tied;
}

class UdpRelay : public PoliceKcmvpRelay
{
public:
    UdpRelay(const Route& route, QObject* parent) : PoliceKcmvpRelay(parent), _route(route) {}

    bool open() override
    {
        _local = new QUdpSocket(this);
        if (!_local->bind(QHostAddress::LocalHost, _route.localPort)) {
            qCWarning(PoliceKcmvpBridgeLog)
                << _route.name << "loopback port" << _route.localPort << "unavailable:" << _local->errorString();
            return false;
        }
        (void) connect(_local, &QUdpSocket::readyRead, this, [this]() { _fromApp(); });
        return true;
    }

    bool setNetwork(qint64 networkHandle) override
    {
        if (_remote) {
            _remote->deleteLater();
            _remote = nullptr;
        }
        const char* const host = (networkHandle != 0) ? _route.kcmvpHost : _route.directHost;
        if (!host) {
            return true;
        }

        auto* const socket = new QUdpSocket(this);
        if (!socket->bind(QHostAddress::AnyIPv4, 0) ||
            ((networkHandle != 0) && !tieToNetwork(socket->socketDescriptor(), networkHandle))) {
            delete socket;
            return false;
        }
        (void) connect(socket, &QUdpSocket::readyRead, this, [this, socket]() { _fromRemote(socket); });
        _remote = socket;
        _target = QHostAddress(QString::fromLatin1(host));
        return true;
    }

private:
    void _fromApp()
    {
        while (_local->hasPendingDatagrams()) {
            const QNetworkDatagram datagram = _local->receiveDatagram();
            _peer = datagram.senderAddress();
            _peerPort = static_cast<quint16>(datagram.senderPort());
            if (_remote) {
                (void) _remote->writeDatagram(datagram.data(), _target, _route.remotePort);
            }
        }
    }

    void _fromRemote(QUdpSocket* socket)
    {
        while (socket->hasPendingDatagrams()) {
            const QNetworkDatagram datagram = socket->receiveDatagram();
            // The module streams to whoever spoke to it last; anything else arriving here is not
            // the device this route leads to.
            const bool fromTarget = (datagram.senderAddress().toIPv4Address() == _target.toIPv4Address()) &&
                                    (datagram.senderPort() == _route.remotePort);
            if ((socket == _remote) && fromTarget && (_peerPort != 0)) {
                (void) _local->writeDatagram(datagram.data(), _peer, _peerPort);
            }
        }
    }

    const Route _route;
    QUdpSocket* _local = nullptr;
    QUdpSocket* _remote = nullptr;
    QHostAddress _target;
    QHostAddress _peer;
    quint16 _peerPort = 0;
};

/// One app connection and its upstream. The camera behind the module is tried first when the
/// USB Ethernet network is up, then the SIYI-side address, so a camera left on the air unit
/// still shows. The camera names itself in RTSP replies (Content-Base rtsp://camera:8554/...),
/// so those are rewritten to the loopback port; otherwise the player would follow the camera's
/// own address, over Wi-Fi.
class TcpSession : public QObject
{
public:
    TcpSession(QTcpSocket* client, const Route& route, qint64 networkHandle, QObject* parent)
        : QObject(parent), _client(client), _route(route)
    {
        _client->setParent(this);
        _upstream = new QTcpSocket(this);
        (void) connect(_client, &QTcpSocket::readyRead, this, [this]() { _fromClient(); });
        (void) connect(_client, &QTcpSocket::disconnected, this, [this]() { _finish(); });
        (void) connect(_upstream, &QTcpSocket::connected, this, [this]() { _onConnected(); });
        (void) connect(_upstream, &QTcpSocket::readyRead, this, [this]() { _fromUpstream(); });
        (void) connect(_upstream, &QTcpSocket::disconnected, this, [this]() { _finish(); });
        (void) connect(_upstream, &QTcpSocket::errorOccurred, this, [this]() { _finish(); });

        _localPrefix = QStringLiteral("rtsp://127.0.0.1:%1").arg(route.localPort).toLatin1();
        _timeout.setSingleShot(true);
        (void) connect(&_timeout, &QTimer::timeout, this, [this]() { _giveUpAttempt(); });

        if ((networkHandle != 0) && route.kcmvpHost) {
            _connectOnNetwork(networkHandle);
        } else {
            _connectDirect();
        }
    }

    ~TcpSession() override { _dropNativeAttempt(); }

private:
    void _useHost(const char* host)
    {
        _remotePrefix = QStringLiteral("rtsp://%1:%2").arg(QLatin1String(host)).arg(_route.remotePort).toLatin1();
        // On 554 the camera may write its address without the port, and a control URL left
        // pointing at it would have the client open a second connection the bridge never sees.
        _remoteBarePrefix = (_route.remotePort == kRtspDefaultPort)
                                ? QStringLiteral("rtsp://%1/").arg(QLatin1String(host)).toLatin1()
                                : QByteArray();
    }

    /// QTcpSocket creates its descriptor inside connectToHost, too late to tie it to a network,
    /// so the connect is done here and the connected descriptor handed over.
    void _connectOnNetwork(qint64 networkHandle)
    {
        _useHost(_route.kcmvpHost);
        _onNetwork = true;
        _timeout.start(kKcmvpAttemptMsecs);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(_route.remotePort);
        if (::inet_pton(AF_INET, _route.kcmvpHost, &address.sin_addr) != 1) {
            _giveUpAttempt();
            return;
        }
        _connectFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if ((_connectFd < 0) || !tieToNetwork(_connectFd, networkHandle)) {
            _giveUpAttempt();
            return;
        }
        if ((::connect(_connectFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) &&
            (errno != EINPROGRESS)) {
            _giveUpAttempt();
            return;
        }

        _notifier = new QSocketNotifier(_connectFd, QSocketNotifier::Write, this);
        (void) connect(_notifier, &QSocketNotifier::activated, this, [this]() { _onNativeConnectDone(); });
    }

    void _connectDirect()
    {
        _onNetwork = false;
        if (!_route.directHost) {
            _finish();
            return;
        }
        _useHost(_route.directHost);
        _timeout.start(kConnectTimeoutMsecs);
        _upstream->connectToHost(QString::fromLatin1(_route.directHost), _route.remotePort);
    }

    void _onNativeConnectDone()
    {
        int error = 0;
        socklen_t length = sizeof(error);
        if ((::getsockopt(_connectFd, SOL_SOCKET, SO_ERROR, &error, &length) != 0) || (error != 0)) {
            _giveUpAttempt();
            return;
        }
        const int fd = _connectFd;
        _connectFd = -1;
        _dropNativeAttempt();
        if (!_upstream->setSocketDescriptor(fd)) {
            (void) ::close(fd);
            _finish();
            return;
        }
        _onConnected();
    }

    /// The camera behind the module did not answer: try the SIYI side. Anything else ends it.
    void _giveUpAttempt()
    {
        if (_connected) {
            return;
        }
        if (_onNetwork) {
            _dropNativeAttempt();
            _connectDirect();
        } else {
            _finish();
        }
    }

    /// Called from inside the notifier's own signal too, so it is disabled and deleted later.
    void _dropNativeAttempt()
    {
        if (_notifier) {
            _notifier->setEnabled(false);
            _notifier->deleteLater();
            _notifier = nullptr;
        }
        if (_connectFd >= 0) {
            (void) ::close(_connectFd);
            _connectFd = -1;
        }
    }

    void _onConnected()
    {
        _timeout.stop();
        _connected = true;
        if (!_pending.isEmpty()) {
            (void) _upstream->write(_pending);
            _pending.clear();
        }
    }

    void _fromClient()
    {
        const QByteArray data = _client->readAll();
        if (_connected) {
            (void) _upstream->write(data);
        } else {
            _pending += data;
        }
    }

    void _fromUpstream()
    {
        QByteArray data = _upstream->readAll();
        // Interleaved RTP starts with '$'; only the text replies carry the camera's address. The
        // headers alone are rewritten, so a body's Content-Length stays true.
        if (data.startsWith("RTSP/")) {
            qsizetype headerEnd = data.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                headerEnd = data.size();
            }
            QByteArray headers = data.left(headerEnd);
            (void) headers.replace(_remotePrefix, _localPrefix);
            if (!_remoteBarePrefix.isEmpty()) {
                (void) headers.replace(_remoteBarePrefix, _localPrefix + '/');
            }
            data = headers + data.mid(headerEnd);
        }
        (void) _client->write(data);
    }

    void _finish()
    {
        if (_finished) {
            return;
        }
        _finished = true;
        _timeout.stop();
        _client->disconnectFromHost();
        // A client that closes right after its last request (rtspsrc's TEARDOWN) still gets it sent.
        (void) _upstream->flush();
        _upstream->abort();
        deleteLater();
    }

    QTcpSocket* _client = nullptr;
    QTcpSocket* _upstream = nullptr;
    const Route _route;
    QSocketNotifier* _notifier = nullptr;
    QTimer _timeout;
    QByteArray _pending;
    QByteArray _remotePrefix;
    QByteArray _remoteBarePrefix;
    QByteArray _localPrefix;
    int _connectFd = -1;
    bool _onNetwork = false;
    bool _connected = false;
    bool _finished = false;
};

class TcpRelay : public PoliceKcmvpRelay
{
public:
    TcpRelay(const Route& route, QObject* parent) : PoliceKcmvpRelay(parent), _route(route) {}

    bool open() override
    {
        _server = new QTcpServer(this);
        if (!_server->listen(QHostAddress::LocalHost, _route.localPort)) {
            qCWarning(PoliceKcmvpBridgeLog)
                << _route.name << "loopback port" << _route.localPort << "unavailable:" << _server->errorString();
            return false;
        }
        (void) connect(_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* const client = _server->nextPendingConnection()) {
                (void) new TcpSession(client, _route, _networkHandle, this);
            }
        });
        return true;
    }

    /// Sessions already open keep the path they started on.
    bool setNetwork(qint64 networkHandle) override
    {
        _networkHandle = networkHandle;
        return true;
    }

private:
    const Route _route;
    QTcpServer* _server = nullptr;
    qint64 _networkHandle = 0;
};
#endif

}  // namespace

Q_APPLICATION_STATIC(PoliceKcmvpBridge, _policeKcmvpBridgeInstance, nullptr);

PoliceKcmvpBridge::PoliceKcmvpBridge(QObject* parent) : QObject(parent) {}

PoliceKcmvpBridge::~PoliceKcmvpBridge() = default;

PoliceKcmvpBridge* PoliceKcmvpBridge::instance()
{
    return _policeKcmvpBridgeInstance();
}

void PoliceKcmvpBridge::start()
{
#ifdef Q_OS_ANDROID
    if (_networkTimer) {
        return;
    }

    const auto add = [this](PoliceKcmvpRelay* relay) {
        if (relay->open()) {
            _relays.append(relay);
        } else {
            delete relay;
        }
    };
    add(new UdpRelay(kMavlinkRoute, this));
    add(new UdpRelay(kPodControlRoute, this));
    add(new TcpRelay(kPodVideoRoute, this));
    add(new TcpRelay(kFpvVideoRoute, this));
    add(new TcpRelay(kPodMediaRoute, this));
    add(new UdpRelay(kAiControlRoute, this));
    add(new TcpRelay(kAiCountRoute, this));
    add(new TcpRelay(kAiVideoRoute, this));

    _networkTimer = new QTimer(this);
    _networkTimer->setInterval(kNetworkPollMsecs);
    (void) connect(_networkTimer, &QTimer::timeout, this, &PoliceKcmvpBridge::_refreshNetwork);
    _networkTimer->start();
    _refreshNetwork();
#endif
}

void PoliceKcmvpBridge::_refreshNetwork()
{
#ifdef Q_OS_ANDROID
    const qint64 handle = findUsbEthernetNetwork();
    const bool sameNetwork = (handle == _networkHandle);
    if (sameNetwork && (_degraded.isEmpty() || !canTieToNetwork(handle))) {
        return;
    }

    // A route that cannot be tied goes to the SIYI side on its own; the others, MAVLink above
    // all, keep the USB Ethernet path.
    const QList<PoliceKcmvpRelay*> routes = sameNetwork ? _degraded : _relays;
    _degraded.clear();
    for (PoliceKcmvpRelay* const relay : routes) {
        if (!relay->setNetwork(handle) && (handle != 0)) {
            (void) relay->setNetwork(0);
            _degraded.append(relay);
        }
    }

    _networkHandle = handle;
    qCDebug(PoliceKcmvpBridgeLog) << ((handle != 0) ? "routes on USB Ethernet network" : "routes on the SIYI side")
                                  << handle << "routes not tied:" << _degraded.size();
#endif
}
