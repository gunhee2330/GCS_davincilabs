#pragma once

#include <QtCore/QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(PoliceLinkDefaultsLog)

/// \brief The links this station keeps or removes on first start.
///
/// Earlier builds added a UDP link named "UniRC 7 (SIYI)", first to 192.168.144.20:19856 and later
/// repointed to 127.0.0.1:19856. The aircraft connects over Bluetooth, so that link does nothing,
/// and on 19856 it competes with the speaker for the SIYI bridge, which answers only whoever spoke
/// to it last. Tablets that ran those builds still have it saved.
namespace PoliceLinkDefaults {
/// Removes every saved UDP link named "UniRC 7 (SIYI)" whose only target is 192.168.144.20:19856
/// or 127.0.0.1:19856, through LinkManager::removeConfiguration() so it is disconnected and the
/// saved list is rewritten without it. Any other link, including one of that name the operator
/// pointed elsewhere, is left alone. Safe to call on every start. Call after
/// LinkManager::loadLinkConfigurationList() and before startAutoConnectedLinks().
void removeLegacyUniRcLink();

/// On Android, starts PoliceKcmvpBridge and adds the auto-connecting UDP link "KCMVP 암호모듈" to
/// its MAVLink port on 127.0.0.1, unless a link of that name is already saved. Does nothing on
/// other platforms. Call after LinkManager::loadLinkConfigurationList() and before
/// startAutoConnectedLinks().
void ensureKcmvpLink();
}  // namespace PoliceLinkDefaults
