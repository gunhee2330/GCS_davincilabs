#pragma once

#include <QtCore/QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(PoliceLinkDefaultsLog)

/// \brief Cleans up the UDP link earlier builds saved on first start.
///
/// Earlier builds added a UDP link named "UniRC 7 (SIYI)" to 192.168.144.20:19856. The aircraft
/// connects over Bluetooth, so that link does nothing, and tablets that ran those builds still
/// have it saved.
namespace PoliceLinkDefaults {
/// Removes every saved UDP link named "UniRC 7 (SIYI)" whose only target is
/// 192.168.144.20:19856, through LinkManager::removeConfiguration() so it is disconnected and the
/// saved list is rewritten without it. Any other link, including one of that name the operator
/// pointed elsewhere, is left alone. Safe to call on every start. Call after
/// LinkManager::loadLinkConfigurationList() and before startAutoConnectedLinks().
void removeLegacyUniRcLink();
}  // namespace PoliceLinkDefaults
