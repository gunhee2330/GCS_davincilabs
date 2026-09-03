#include "PoliceLinkDefaults.h"

#include <QtCore/QLatin1String>
#include <QtCore/QVariant>

#include "LinkConfiguration.h"
#include "LinkManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "UDPLink.h"

QGC_LOGGING_CATEGORY(PoliceLinkDefaultsLog, "PoliceDrone.LinkDefaults")

namespace {

constexpr const char* kUniRcLinkName = "UniRC 7 (SIYI)";
constexpr const char* kUniRcHost = "192.168.144.20";
constexpr quint16 kUniRcPort = 19856;

/// The manager exposes its configurations only as the QML list model; read it through the
/// property rather than adding an accessor to a file the other branches also touch.
bool linkExists(LinkManager* manager, const QLatin1String& name)
{
    const auto* const configs = manager->property("linkConfigurations").value<QmlObjectListModel*>();
    if (!configs) {
        return false;
    }
    for (int i = 0; i < configs->count(); ++i) {
        const auto* const config = qobject_cast<const LinkConfiguration*>((*configs)[i]);
        if (config && (config->name() == name)) {
            return true;
        }
    }
    return false;
}

}  // namespace

void PoliceLinkDefaults::ensureUniRcLink()
{
    LinkManager* const manager = LinkManager::instance();
    const QLatin1String name(kUniRcLinkName);
    if (linkExists(manager, name)) {
        return;
    }

    LinkConfiguration* const created = manager->createConfiguration(LinkConfiguration::TypeUdp, name);
    auto* const udp = qobject_cast<UDPConfiguration*>(created);
    if (!udp) {
        qCWarning(PoliceLinkDefaultsLog) << "UDP link type unavailable; UniRC 7 link not created";
        delete created;
        return;
    }

    // Port 0 is what SIYI's own instructions ask for: the socket takes an ephemeral port and
    // the ground unit answers to whichever port our heartbeats come from.
    udp->addHost(QLatin1String(kUniRcHost), kUniRcPort);
    udp->setLocalPort(0);
    udp->setAutoConnect(true);
    manager->endCreateConfiguration(udp);
    qCDebug(PoliceLinkDefaultsLog) << "created" << name << kUniRcHost << kUniRcPort;
}
