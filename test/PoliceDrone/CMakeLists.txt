# ============================================================================
# PoliceDrone Tests
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        PoliceLidarMonitorTest.cc
        PoliceLidarMonitorTest.h
        TakeoffCounterTest.cc
        TakeoffCounterTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(PoliceLidarMonitorTest LABELS Integration Vehicle)
add_qgc_test(TakeoffCounterTest LABELS Integration Vehicle)
