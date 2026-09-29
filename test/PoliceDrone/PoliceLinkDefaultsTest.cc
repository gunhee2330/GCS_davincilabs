#include "PoliceLinkDefaultsTest.h"

#include <QtCore/QVariant>
#include <QtTest/QTest>

#include "LinkManager.h"
#include "PoliceLinkDefaults.h"
#include "QmlObjectListModel.h"
#include "UDPLink.h"

namespace {

const QString kLegacyName = QStringLiteral("UniRC 7 (SIYI)");

QmlObjectListModel* configs()
{
    return LinkManager::instance()->property("linkConfigurations").value<QmlObjectListModel*>();
}

/// "name host:port" for every configuration LinkManager holds, in list order.
QStringList links()
{
    QStringList result;
    for (int i = 0; i < configs()->count(); ++i) {
        const auto* const udp = configs()->value<UDPConfiguration*>(i);
        result.append(udp ? (udp->name() + QLatin1Char(' ') + udp->hostList().join(QLatin1Char(',')))
                          : configs()->value<LinkConfiguration*>(i)->name());
    }
    return result;
}

/// Adds a UDP link the way the Comm Links page does, which also saves the list to QSettings.
void addUdpLink(const QString& name, const QString& host, quint16 port)
{
    LinkManager* const manager = LinkManager::instance();
    auto* const udp = qobject_cast<UDPConfiguration*>(manager->createConfiguration(LinkConfiguration::TypeUdp, name));
    QVERIFY(udp);
    udp->addHost(host, port);
    udp->setAutoConnect(true);
    manager->endCreateConfiguration(udp);
}

void addLegacyLink()
{
    addUdpLink(kLegacyName, QStringLiteral("192.168.144.20"), 19856);
}

} // namespace

void PoliceLinkDefaultsTest::cleanup()
{
    for (int i = configs()->count() - 1; i >= 0; --i) {
        LinkManager::instance()->removeConfiguration(configs()->value<LinkConfiguration*>(i));
    }
    UnitTest::cleanup();
}

void PoliceLinkDefaultsTest::_freshSettingsHaveNoLink_test()
{
    QVERIFY(links().isEmpty());
    PoliceLinkDefaults::removeLegacyUniRcLink();
    QVERIFY(links().isEmpty());
}

void PoliceLinkDefaultsTest::_legacyLinkIsRemovedFromSettings_test()
{
    addLegacyLink();
    QCOMPARE(links(), QStringList{kLegacyName + QStringLiteral(" 192.168.144.20:19856")});

    PoliceLinkDefaults::removeLegacyUniRcLink();
    QVERIFY(links().isEmpty());

    LinkManager::instance()->loadLinkConfigurationList();
    QVERIFY(links().isEmpty());
}

void PoliceLinkDefaultsTest::_loopbackLegacyLinkIsRemoved_test()
{
    addUdpLink(kLegacyName, QStringLiteral("127.0.0.1"), 19856);
    QCOMPARE(links(), QStringList{kLegacyName + QStringLiteral(" 127.0.0.1:19856")});

    PoliceLinkDefaults::removeLegacyUniRcLink();
    QVERIFY(links().isEmpty());

    LinkManager::instance()->loadLinkConfigurationList();
    QVERIFY(links().isEmpty());
}

void PoliceLinkDefaultsTest::_otherLinksAreKept_test()
{
    addUdpLink(QStringLiteral("Bench UDP"), QStringLiteral("192.168.144.20"), 19856);
    addLegacyLink();
    addUdpLink(kLegacyName, QStringLiteral("10.0.0.5"), 14550);

    PoliceLinkDefaults::removeLegacyUniRcLink();
    const QStringList kept{QStringLiteral("Bench UDP 192.168.144.20:19856"),
                           kLegacyName + QStringLiteral(" 10.0.0.5:14550")};
    QCOMPARE(links(), kept);

    // Loading from the same settings appends the saved list again: the same two, nothing else.
    LinkManager::instance()->loadLinkConfigurationList();
    QCOMPARE(links(), kept + kept);
}

void PoliceLinkDefaultsTest::_secondCallIsHarmless_test()
{
    addUdpLink(QStringLiteral("Bench UDP"), QStringLiteral("10.0.0.5"), 14550);
    addLegacyLink();

    PoliceLinkDefaults::removeLegacyUniRcLink();
    PoliceLinkDefaults::removeLegacyUniRcLink();
    QCOMPARE(links(), QStringList{QStringLiteral("Bench UDP 10.0.0.5:14550")});

    LinkManager::instance()->loadLinkConfigurationList();
    QCOMPARE(links(), QStringList(2, QStringLiteral("Bench UDP 10.0.0.5:14550")));
}

UT_REGISTER_TEST(PoliceLinkDefaultsTest, TestLabel::Unit, TestLabel::Comms)
