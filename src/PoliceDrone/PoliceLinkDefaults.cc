#include "PoliceLinkDefaults.h"

#include <QtCore/QStringList>
#include <QtCore/QVariant>

#include "LinkManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "UDPLink.h"

QGC_LOGGING_CATEGORY(PoliceLinkDefaultsLog, "PoliceDrone.LinkDefaults")

void PoliceLinkDefaults::removeLegacyUniRcLink()
{
    LinkManager* const manager = LinkManager::instance();
    // The manager exposes its configurations only as the QML list model; read it through the
    // property rather than adding an accessor to a file the other branches also touch.
    const auto* const configs = manager->property("linkConfigurations").value<QmlObjectListModel*>();
    if (!configs) {
        return;
    }

    const QString legacyName = QStringLiteral("UniRC 7 (SIYI)");
    const QStringList legacyTarget{QStringLiteral("192.168.144.20:19856")};
    // Backwards so a removal does not shift the entries still to be checked.
    for (int i = configs->count() - 1; i >= 0; --i) {
        auto* const udp = configs->value<UDPConfiguration*>(i);
        if (!udp || (udp->name() != legacyName) || (udp->hostList() != legacyTarget)) {
            continue;
        }
        qCInfo(PoliceLinkDefaultsLog) << "removing legacy link" << udp->name() << udp->hostList();
        manager->removeConfiguration(udp);
    }
}
