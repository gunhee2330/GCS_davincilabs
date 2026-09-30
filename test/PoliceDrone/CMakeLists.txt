# ============================================================================
# PoliceDrone Tests
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        FlightRecordsTest.cc
        FlightRecordsTest.h
        PinGateTest.cc
        PinGateTest.h
        PoliceLidarMonitorTest.cc
        PoliceLidarMonitorTest.h
        PoliceLinkDefaultsTest.cc
        PoliceLinkDefaultsTest.h
        PoliceRcButtonsTest.cc
        PoliceRcButtonsTest.h
        PoliceRcLinkTest.cc
        PoliceRcLinkTest.h
        PoliceRcKeysTest.cc
        PoliceRcKeysTest.h
        PoliceWarningsTest.cc
        PoliceWarningsTest.h
        TakeoffCounterTest.cc
        TakeoffCounterTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(FlightRecordsTest LABELS Integration Vehicle)
add_qgc_test(PinGateTest LABELS Unit)
add_qgc_test(PoliceLidarMonitorTest LABELS Integration Vehicle)
add_qgc_test(PoliceLinkDefaultsTest LABELS Unit Comms)
add_qgc_test(PoliceRcButtonsTest LABELS Unit)
add_qgc_test(PoliceRcKeysTest LABELS Unit)
add_qgc_test(PoliceRcLinkTest LABELS Unit)
add_qgc_test(PoliceWarningsTest LABELS Integration Vehicle)
add_qgc_test(TakeoffCounterTest LABELS Integration Vehicle)
