# ============================================================================
# Police Drone Module
# Ground-station bookkeeping the procurement spec asks for and no autopilot
# reports: the per-airframe takeoff count, the links and payload streams a fresh
# install starts with, and the handheld's buttons wired to the SIYI pod.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        PoliceGimbalJoystick.cc
        PoliceGimbalJoystick.h
        PoliceLinkDefaults.cc
        PoliceLinkDefaults.h
        PoliceVideoDefaults.cc
        PoliceVideoDefaults.h
        TakeoffCounter.cc
        TakeoffCounter.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
