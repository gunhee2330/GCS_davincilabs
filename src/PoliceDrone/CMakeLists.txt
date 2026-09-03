# ============================================================================
# Police Drone Module
# Ground-station bookkeeping the procurement spec asks for and no autopilot
# reports: currently the per-airframe takeoff count.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        TakeoffCounter.cc
        TakeoffCounter.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
