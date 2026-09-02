# ============================================================================
# Speaker Module
# Loudspeaker payload control over the aircraft network
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        SpeakerController.cc
        SpeakerController.h
        SpeakerProtocol.cc
        SpeakerProtocol.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
