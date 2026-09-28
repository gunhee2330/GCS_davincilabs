# ============================================================================
# SIYI Camera Unit Tests
# Tests for the SIYI SDK protocol codec
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        MissionAutoRecordTest.cc
        MissionAutoRecordTest.h
        SiyiAiControllerTest.cc
        SiyiAiControllerTest.h
        SiyiCameraControllerTest.cc
        SiyiCameraControllerTest.h
        SiyiLongProtocolTest.cc
        SiyiLongProtocolTest.h
        SiyiProtocolTest.cc
        SiyiProtocolTest.h
        SiyiRecordingsTest.cc
        SiyiRecordingsTest.h
        FakePodMediaServer.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(SiyiProtocolTest LABELS Unit)
add_qgc_test(SiyiLongProtocolTest LABELS Unit)
add_qgc_test(SiyiAiControllerTest LABELS Unit)
add_qgc_test(SiyiCameraControllerTest LABELS Unit)
add_qgc_test(MissionAutoRecordTest LABELS Integration Vehicle)
add_qgc_test(SiyiRecordingsTest LABELS Unit RESOURCE_LOCK TempFiles)

# ----------------------------------------------------------------------------
# Test Data Resources
# ----------------------------------------------------------------------------
qt_add_resources(${CMAKE_PROJECT_NAME} "SiyiCameraTest_res"
    PREFIX "/unittest"
    FILES
        pod_sample.mp4
)
