# ============================================================================
# Person Detection Module
# On-station person detection for the EO stream: live head-count and head
# mosaic on screen, and blurring of finished recordings.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        PersonDetectorCore.cc
        PersonDetectorCore.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
