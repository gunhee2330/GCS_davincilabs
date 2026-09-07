# ============================================================================
# PersonDetection Tests
# Pure-logic tests for the letterbox / decode / NMS core, plus an
# end-to-end run of the ONNX model (skipped when it is not built in)
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        PersonDetectorCoreTest.cc
        PersonDetectorCoreTest.h
        PersonDetectorTest.cc
        PersonDetectorTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
# Test Data Resources
# ----------------------------------------------------------------------------
qt_add_resources(${CMAKE_PROJECT_NAME} "PersonDetectionTest_res"
    PREFIX "/unittest"
    FILES
        bus.jpg
)

add_qgc_test(PersonDetectorCoreTest LABELS Unit)
add_qgc_test(PersonDetectorTest LABELS Unit)
