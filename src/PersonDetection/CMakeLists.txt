# ============================================================================
# Person Detection Module
# On-station person detection for the EO stream: head mosaic drawn over the
# live picture. Recordings are NOT touched - the mosaic is an overlay on the
# VideoOutput, so a file on the SD card or on disk carries unmasked faces.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        PersonDetector.cc
        PersonDetector.h
        PersonDetectorCore.cc
        PersonDetectorCore.h
        PersonDetectorWorker.cc
        PersonDetectorWorker.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Without ONNX Runtime the classes still build; the worker fails to load and the
# detector stays inactive, so no caller needs an #ifdef.
if(QGC_ENABLE_PERSON_DETECTION)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE onnxruntime::onnxruntime)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_HAS_ONNXRUNTIME)

    qt_add_resources(${CMAKE_PROJECT_NAME} persondetection_model
        PREFIX "/PersonDetection"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/models"
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/models/rtmdet-n-person.onnx"
            "${CMAKE_CURRENT_SOURCE_DIR}/models/rtmdet-n-person.json"
    )
endif()
