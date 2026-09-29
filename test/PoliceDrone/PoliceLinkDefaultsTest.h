#pragma once

#include "UnitTest.h"

/// Tests PoliceLinkDefaults::removeLegacyUniRcLink() against LinkManager's saved link list.
/// UnitTest clears QSettings before every test function; cleanup() empties LinkManager's list.
class PoliceLinkDefaultsTest : public UnitTest
{
    Q_OBJECT

protected slots:
    void cleanup() override;

private slots:
    /// Nothing saved: the call adds nothing.
    void _freshSettingsHaveNoLink_test();

    /// The saved legacy link is gone after the call, and loading the list again from the same
    /// settings does not bring it back.
    void _legacyLinkIsRemovedFromSettings_test();

    /// The same link as later builds repointed it, to the bridge on loopback, goes too.
    void _loopbackLegacyLinkIsRemoved_test();

    /// Beside the legacy link, a user link and a link of the same name pointed elsewhere stay.
    void _otherLinksAreKept_test();

    /// A second call leaves the list and the settings as the first one did.
    void _secondCallIsHarmless_test();
};
