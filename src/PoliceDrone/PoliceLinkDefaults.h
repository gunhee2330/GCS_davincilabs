#pragma once

#include <QtCore/QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(PoliceLinkDefaultsLog)

/// \brief The links this station ships with, created on a fresh install.
///
/// The UniRC 7 Pro hands MAVLink to apps on the handheld over UDP: the app sends to the
/// ground unit at 192.168.144.20:19856 and the vehicle's stream comes back the same way
/// (UniRC 7 manual §4.3.1). An operator should not have to type that in, so the link exists
/// from first start and connects on its own; it stays editable under Comm Links.
namespace PoliceLinkDefaults {
/// Adds the UniRC 7 link unless a configuration of that name already exists. Call after
/// LinkManager::init() has loaded the saved list and before startAutoConnectedLinks().
void ensureUniRcLink();
}  // namespace PoliceLinkDefaults
