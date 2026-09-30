#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QString>

Q_DECLARE_LOGGING_CATEGORY(PoliceVideoDefaultsLog)

/// \brief The primary video stream this station ships with, set on a fresh install.
///
/// The pod's main stream is the picture the operator flies on, but QGC ships with video
/// disabled and an empty URL because it cannot know what payload is fitted. This station
/// does: the ZT30 publishes rtsp://192.168.144.26:8554/video1 over the SIYI link. The pod
/// sub stream, the AI module feed and the FPV camera all carry their addresses as setting
/// defaults; only these two facts need code, because their metadata is shared with every
/// other QGC build.
///
/// On Android the pod and FPV addresses go through PoliceKcmvpBridge on loopback instead, so the
/// payload is reached behind the KCMVP encryption module when the handset's USB Ethernet adapter
/// is plugged in; settings still holding the shipped SIYI-side addresses are moved there.
namespace PoliceVideoDefaults {
/// Points the main video panel at the pod unless the operator has already chosen a source.
/// Call once at startup, after SettingsManager is up and PoliceKcmvpBridge has started.
void ensurePodStream();

/// Fixed rtspsrc UDP port ranges, one block per payload stream. With random ports a session the
/// camera still runs from before a link drop can keep sending to a port a later SETUP of another
/// window binds, and that window shows the wrong camera. Each stream owning its block means a
/// leftover can only land on its own window. 10 ports = 5 RTP/RTCP pairs for rtspsrc's retries.
/// ponytail: fixed block below the Android ephemeral range (32768+); moves if another app on the
/// handset turns out to bind 25000-25039.
inline constexpr const char* kPodMainPortRange = "25000-25009";
inline constexpr const char* kPodSubPortRange = "25010-25019";
inline constexpr const char* kAiPortRange = "25020-25029";
inline constexpr const char* kFpvPortRange = "25030-25039";

/// True for the pod main stream this station ships with, direct or through the bridge. Any other
/// URL in the stock RTSP setting is the operator's own source and keeps stock random ports.
bool isPodMainStream(const QString& uri);
}  // namespace PoliceVideoDefaults
