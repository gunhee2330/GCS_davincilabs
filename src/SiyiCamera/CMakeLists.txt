# ============================================================================
# SIYI Camera Module
# Direct control of SIYI optical pods over the vendor SDK protocol
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        SiyiAiController.cc
        SiyiAiController.h
        SiyiAiProtocol.cc
        SiyiAiProtocol.h
        SiyiCameraController.cc
        SiyiCameraController.h
        SiyiProtocol.cc
        SiyiProtocol.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
# SIYI Camera QML Module
# ----------------------------------------------------------------------------
qt_add_library(SiyiCameraModule STATIC)

qt_add_qml_module(SiyiCameraModule
    URI QGroundControl.SiyiCamera
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        SiyiCameraControlPanel.qml
        SiyiCameraToolStripAction.qml
    NO_PLUGIN
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE SiyiCameraModule)
