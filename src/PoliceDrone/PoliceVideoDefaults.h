#pragma once

#include <QtCore/QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(PoliceVideoDefaultsLog)

/// \brief The primary video stream this station ships with, set on a fresh install.
///
/// The pod's main stream is the picture the operator flies on, but QGC ships with video
/// disabled and an empty URL because it cannot know what payload is fitted. This station
/// does: the ZT30 publishes rtsp://192.168.144.26:8554/video1 over the SIYI link. The pod
/// sub stream, the AI module feed and the FPV camera all carry their addresses as setting
/// defaults; only these two facts need code, because their metadata is shared with every
/// other QGC build.
namespace PoliceVideoDefaults {
/// Points the main video panel at the pod unless the operator has already chosen a source.
/// Call once at startup, after SettingsManager is up.
void ensurePodStream();
}  // namespace PoliceVideoDefaults
