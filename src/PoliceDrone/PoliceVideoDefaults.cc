#include "PoliceVideoDefaults.h"

#include <QtCore/QLatin1String>
#include <QtCore/QVariant>

#include "Fact.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "VideoSettings.h"

#ifdef Q_OS_ANDROID
#include "PoliceKcmvpBridge.h"
#include "SiyiCameraSettings.h"
#endif

QGC_LOGGING_CATEGORY(PoliceVideoDefaultsLog, "PoliceDrone.VideoDefaults")

namespace {

/// The pod sits on the air unit's LAN1 port, readdressed from the SIYI factory .25 so the
/// FPV camera on LAN2 can keep it. /video1 is the main stream, /video2 the sub stream.
constexpr const char* kPodMainStream = "rtsp://192.168.144.26:8554/video1";

#ifdef Q_OS_ANDROID
/// Settings still holding the SIYI-side address this station shipped with are moved to the
/// bridge; anything an operator typed stays.
void repoint(Fact* fact, const QString& shipped, const QString& bridged)
{
    if (fact && (fact->rawValue().toString() == shipped)) {
        fact->setRawValue(bridged);
        qCDebug(PoliceVideoDefaultsLog) << fact->name() << "moved to" << bridged;
    }
}

QString bridgedPodStream(const char* path)
{
    return QStringLiteral("rtsp://127.0.0.1:%1/%2").arg(PoliceKcmvpBridge::PodVideoPort).arg(QLatin1String(path));
}

/// On the handset the payload is reached through PoliceKcmvpBridge on loopback, which picks the
/// path behind the encryption module or the SIYI side by itself.
void useKcmvpBridge(VideoSettings* video)
{
    repoint(video->rtspUrl(), QLatin1String(kPodMainStream), bridgedPodStream("video1"));

    SiyiCameraSettings* const siyi = SettingsManager::instance()->siyiCameraSettings();
    if (!siyi) {
        return;
    }
    // The bridge forwards its one loopback port to the pod's 37260; a pod on another port stays put.
    if (siyi->port()->rawValue().toUInt() == PoliceKcmvpBridge::PodControlPort) {
        repoint(siyi->ipAddress(), QStringLiteral("192.168.144.26"), QStringLiteral("127.0.0.1"));
    }
    repoint(siyi->secondaryRtspUrl(), QStringLiteral("rtsp://192.168.144.26:8554/video2"), bridgedPodStream("video2"));
    repoint(siyi->fpvRtspUrl(), QStringLiteral("rtsp://192.168.144.25:8554/main.264"),
            QStringLiteral("rtsp://127.0.0.1:%1/main.264").arg(PoliceKcmvpBridge::FpvVideoPort));
}
#endif

QString podMainStream()
{
#ifdef Q_OS_ANDROID
    return bridgedPodStream("video1");
#else
    return QLatin1String(kPodMainStream);
#endif
}

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

#ifdef Q_OS_ANDROID
    useKcmvpBridge(settings);
#endif

    // Only fill in a station that has never been configured. Once an operator has picked a
    // source these are their settings, including a deliberate choice of no video at all.
    if (sourceFact->rawValue().toString() != QLatin1String(VideoSettings::videoDisabled)) {
        return;
    }
    if (!urlFact->rawValue().toString().trimmed().isEmpty()) {
        return;
    }

    const QString stream = podMainStream();
    urlFact->setRawValue(stream);
    sourceFact->setRawValue(QLatin1String(VideoSettings::videoSourceRTSP));
    qCDebug(PoliceVideoDefaultsLog) << "primary stream set to" << stream;
}

bool PoliceVideoDefaults::isPodMainStream(const QString& uri)
{
    const QString trimmed = uri.trimmed();
    return (trimmed == QLatin1String(kPodMainStream)) || (trimmed == podMainStream());
}
