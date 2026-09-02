# ============================================================================
# SIYI Camera Unit Tests
# Tests for the SIYI SDK protocol codec
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        SiyiProtocolTest.cc
        SiyiProtocolTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(SiyiProtocolTest LABELS Unit)
