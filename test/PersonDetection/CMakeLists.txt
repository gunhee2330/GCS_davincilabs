# ============================================================================
# PersonDetection Tests
# Pure-logic tests for the YOLOv8 letterbox / decode / NMS core
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        PersonDetectorCoreTest.cc
        PersonDetectorCoreTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(PersonDetectorCoreTest LABELS Unit)
