# ============================================================================
# Police Drone Module
# Ground-station bookkeeping the procurement spec asks for and no autopilot
# reports: the per-airframe takeoff count, and the links a fresh install starts with.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        PoliceLinkDefaults.cc
        PoliceLinkDefaults.h
        TakeoffCounter.cc
        TakeoffCounter.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
