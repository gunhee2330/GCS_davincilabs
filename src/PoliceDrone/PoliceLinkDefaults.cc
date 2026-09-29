#include "PoliceLinkDefaults.h"

#include <QtCore/QStringList>
#include <QtCore/QVariant>

#include "LinkManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "UDPLink.h"

#ifdef Q_OS_ANDROID
#include "LinkConfiguration.h"
#include "PoliceKcmvpBridge.h"
#endif

QGC_LOGGING_CATEGORY(PoliceLinkDefaultsLog, "PoliceDrone.LinkDefaults")

namespace {

/// The manager exposes its configurations only as the QML list model; read it through the
/// property rather than adding an accessor to a file the other branches also touch.
QmlObjectListModel* linkConfigurations(LinkManager* manager)
{
    return manager->property("linkConfigurations").value<QmlObjectListModel*>();
}

}  // namespace

void PoliceLinkDefaults::removeLegacyUniRcLink()
{
    LinkManager* const manager = LinkManager::instance();
    const auto* const configs = linkConfigurations(manager);
    if (!configs) {
        return;
    }

    const QString legacyName = QStringLiteral("UniRC 7 (SIYI)");
    // The first builds aimed it at the ground unit's address; later ones repointed it to loopback,
    // where it kept talking to the bridge the speaker needs to itself.
    const QList<QStringList> legacyTargets{{QStringLiteral("192.168.144.20:19856")},
                                           {QStringLiteral("127.0.0.1:19856")}};
    // Backwards so a removal does not shift the entries still to be checked.
    for (int i = configs->count() - 1; i >= 0; --i) {
        auto* const udp = configs->value<UDPConfiguration*>(i);
        if (!udp || (udp->name() != legacyName) || !legacyTargets.contains(udp->hostList())) {
            continue;
        }
        qCInfo(PoliceLinkDefaultsLog) << "removing legacy link" << udp->name() << udp->hostList();
        manager->removeConfiguration(udp);
    }
}

void PoliceLinkDefaults::ensureKcmvpLink()
{
#ifdef Q_OS_ANDROID
    PoliceKcmvpBridge::instance()->start();

    LinkManager* const manager = LinkManager::instance();
    const auto* const configs = linkConfigurations(manager);
    if (!configs) {
        return;
    }

    const QString name = QStringLiteral("KCMVP 암호모듈");
    for (int i = 0; i < configs->count(); ++i) {
        const auto* const config = configs->value<LinkConfiguration*>(i);
        if (config && (config->name() == name)) {
            return;
        }
    }

    LinkConfiguration* const created = manager->createConfiguration(LinkConfiguration::TypeUdp, name);
    auto* const udp = qobject_cast<UDPConfiguration*>(created);
    if (!udp) {
        qCWarning(PoliceLinkDefaultsLog) << "UDP link type unavailable; KCMVP link not created";
        delete created;
        return;
    }

    udp->addHost(QStringLiteral("127.0.0.1"), PoliceKcmvpBridge::LinkPort);
    udp->setAutoConnect(true);
    // setAutoConnect moves the link onto QGC's standard listen port, which QGC's own UDP
    // auto-connect link already holds; two sockets sharing it split each other's replies. The
    // bridge answers whatever port the link sends from, so any free one will do.
    udp->setLocalPort(0);
    manager->endCreateConfiguration(udp);
    qCDebug(PoliceLinkDefaultsLog) << "created" << name << "127.0.0.1" << PoliceKcmvpBridge::LinkPort;
#endif
}
