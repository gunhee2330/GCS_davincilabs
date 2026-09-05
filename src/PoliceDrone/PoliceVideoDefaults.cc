#include "PoliceVideoDefaults.h"

#include <QtCore/QLatin1String>
#include <QtCore/QVariant>

#include "Fact.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "VideoSettings.h"

QGC_LOGGING_CATEGORY(PoliceVideoDefaultsLog, "PoliceDrone.VideoDefaults")

namespace {

/// The pod sits on the air unit's LAN1 port, readdressed from the SIYI factory .25 so the
/// FPV camera on LAN2 can keep it. /video1 is the main stream, /video2 the sub stream.
constexpr const char* kPodMainStream = "rtsp://192.168.144.26:8554/video1";

}  // namespace

void PoliceVideoDefaults::ensurePodStream()
{
    VideoSettings* const settings = SettingsManager::instance()->videoSettings();
    if (!settings) {
        return;
    }

    Fact* const sourceFact = settings->videoSource();
    Fact* const urlFact = settings->rtspUrl();
    if (!sourceFact || !urlFact) {
        return;
    }

    // Only fill in a station that has never been configured. Once an operator has picked a
    // source these are their settings, including a deliberate choice of no video at all.
    if (sourceFact->rawValue().toString() != QLatin1String(VideoSettings::videoDisabled)) {
        return;
    }
    if (!urlFact->rawValue().toString().trimmed().isEmpty()) {
        return;
    }

    urlFact->setRawValue(QLatin1String(kPodMainStream));
    sourceFact->setRawValue(QLatin1String(VideoSettings::videoSourceRTSP));
    qCDebug(PoliceVideoDefaultsLog) << "primary stream set to" << kPodMainStream;
}
