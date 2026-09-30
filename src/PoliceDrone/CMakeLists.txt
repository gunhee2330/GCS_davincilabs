# ============================================================================
# Police Drone Module
# Ground-station bookkeeping the procurement spec asks for and no autopilot
# reports: the per-airframe takeoff count, the payload streams a fresh
# install starts with, the handheld's buttons wired to the SIYI pod, the
# developer PIN, the loopback bridge to the KCMVP encryption modules, and push-to-talk
# from the handset's microphone to the loudspeaker payload.
# ============================================================================

# The developer PIN a fresh install accepts, kept as a hash and replaceable from the developer
# page after that. Set it on the configure line of a real build: this repository is public, so a
# value written here is a value published.
set(QGC_DEVELOPER_PIN "000000" CACHE STRING "Developer PIN a fresh install accepts (4 to 8 digits)")
if(NOT QGC_DEVELOPER_PIN MATCHES "^[0-9][0-9][0-9][0-9][0-9]?[0-9]?[0-9]?[0-9]?$")
    message(FATAL_ERROR "QGC_DEVELOPER_PIN must be 4 to 8 digits, got '${QGC_DEVELOPER_PIN}'")
endif()

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        ControllerBattery.cc
        ControllerBattery.h
        DeveloperPin.cc
        DeveloperPin.h
        FlightRecords.cc
        FlightRecords.h
        PinGate.cc
        PinGate.h
        PoliceCorePlugin.cc
        PoliceCorePlugin.h
        PoliceDesktopScale.cc
        PoliceDesktopScale.h
        PoliceGimbalJoystick.cc
        PoliceGimbalJoystick.h
        PoliceKcmvpBridge.cc
        PoliceKcmvpBridge.h
        PoliceLidarMonitor.cc
        PoliceLidarMonitor.h
        PoliceLinkDefaults.cc
        PoliceLinkDefaults.h
        PoliceLiveVoice.cc
        PoliceLiveVoice.h
        PoliceRcButtons.cc
        PoliceRcButtons.h
        PoliceRcLink.cc
        PoliceRcLink.h
        PoliceRcKeys.cc
        PoliceRcKeys.h
        PoliceSpeechTranslator.cc
        PoliceSpeechTranslator.h
        PoliceVideoDefaults.cc
        PoliceVideoDefaults.h
        PoliceWarnings.cc
        PoliceWarnings.h
        TakeoffCounter.cc
        TakeoffCounter.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# PoliceKcmvpBridge calls android_setsocknetwork from the NDK's libandroid. Today GStreamer's
# plugins happen to put it on the link line; a build without video streaming would not.
if(ANDROID)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE android)
endif()

# PoliceDesktopScale reads the main display's width before Qt starts.
if(MACOS)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "-framework CoreGraphics")
endif()

# On the one file that reads it, so changing the PIN does not rebuild the tree. The target is made
# in the root directory, and a source property set from here reaches it only through
# TARGET_DIRECTORY; without it the define never arrived and every build shipped 000000.
set_source_files_properties(DeveloperPin.cc TARGET_DIRECTORY ${CMAKE_PROJECT_NAME} PROPERTIES
    COMPILE_DEFINITIONS "QGC_DEVELOPER_PIN=\"${QGC_DEVELOPER_PIN}\""
)
