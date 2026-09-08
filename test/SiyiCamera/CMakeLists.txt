# ============================================================================
# SIYI Camera Unit Tests
# Tests for the SIYI SDK protocol codec
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        SiyiAiControllerTest.cc
        SiyiAiControllerTest.h
        SiyiCameraControllerTest.cc
        SiyiCameraControllerTest.h
        SiyiLongProtocolTest.cc
        SiyiLongProtocolTest.h
        SiyiProtocolTest.cc
        SiyiProtocolTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(SiyiProtocolTest LABELS Unit)
add_qgc_test(SiyiLongProtocolTest LABELS Unit)
add_qgc_test(SiyiAiControllerTest LABELS Unit)
add_qgc_test(SiyiCameraControllerTest LABELS Unit)
