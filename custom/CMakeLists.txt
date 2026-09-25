# ============================================================================
# Police build overlay
# QGC's own custom-build hook, used for one thing: QGCCorePlugin::instance()
# hands out the fork's PoliceCorePlugin (src/PoliceDrone) instead of the stock
# plugin. Nothing here renames, rebrands or repackages the app.
# ============================================================================

set(CUSTOM_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="PoliceCorePlugin.h"
    CUSTOMCLASS=PoliceCorePlugin
    PARENT_SCOPE
)
