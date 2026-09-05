include_guard(GLOBAL)
include(Download)

# ONNX Runtime prebuilt for on-device person detection. Only the two shipping targets have a
# pinned binary — the Android arm64 tablet and the arm64 macOS dev host — so everywhere else
# the feature is off and PersonDetector stays inactive at runtime.

set(_ort_version "1.25.1")
set(_ort_macos_hash "SHA256=18987ec3187b5f29ba798109750f6135060560ad4e0a52678fcc753ee8fb3091")
set(_ort_android_hash "SHA256=08ccb60c93bd027843e8227743ddcb0275a00ee706cb0df4bae91cc1a21a55f5")

if((ANDROID AND CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
   OR (MACOS AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64"))
    set(_ort_supported ON)
else()
    set(_ort_supported OFF)
endif()

option(QGC_ENABLE_PERSON_DETECTION "Enable on-device person detection (ONNX Runtime)" ${_ort_supported})

if(NOT QGC_ENABLE_PERSON_DETECTION)
    message(STATUS "QGC: Person detection disabled")
    return()
endif()
if(NOT _ort_supported)
    message(FATAL_ERROR "QGC: QGC_ENABLE_PERSON_DETECTION is on but no ONNX Runtime ${_ort_version} prebuilt "
                        "is pinned for this platform (Android arm64-v8a and arm64 macOS only)")
endif()

if(ANDROID)
    # Maven ships the Android build as an .aar; CPM only unpacks recognised archive suffixes,
    # so it is cached under a .zip name (an .aar is a zip).
    set(_ort_filename "onnxruntime-android-${_ort_version}.zip")
    set(_ort_url "https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/${_ort_version}/onnxruntime-android-${_ort_version}.aar")
    set(_ort_hash "${_ort_android_hash}")
else()
    set(_ort_filename "onnxruntime-osx-arm64-${_ort_version}.tgz")
    set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${_ort_version}/onnxruntime-osx-arm64-${_ort_version}.tgz")
    set(_ort_hash "${_ort_macos_hash}")
endif()

if(CPM_SOURCE_CACHE)
    set(_ort_dl_dir "${CPM_SOURCE_CACHE}/onnxruntime")
else()
    set(_ort_dl_dir "${CMAKE_BINARY_DIR}/_deps/onnxruntime-dl")
endif()
qgc_resilient_download(
    FILENAME "${_ort_filename}"
    DESTINATION_DIR "${_ort_dl_dir}"
    RESULT_VAR _ort_archive
    URLS "${_ort_url}"
    EXPECTED_HASH "${_ort_hash}"
    LOG_TAG "ONNX Runtime"
)
CPMAddPackage(
    NAME onnxruntime
    URL "${_ort_archive}"
    URL_HASH "${_ort_hash}"
    DOWNLOAD_ONLY YES
)

qgc_require_cpm_added(onnxruntime)

if(ANDROID)
    set(_ort_include_dir "${onnxruntime_SOURCE_DIR}/headers")
    set(_ort_library "${onnxruntime_SOURCE_DIR}/jni/${CMAKE_ANDROID_ARCH_ABI}/libonnxruntime.so")
else()
    set(_ort_include_dir "${onnxruntime_SOURCE_DIR}/include")
    set(_ort_library "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.${_ort_version}.dylib")
endif()
if(NOT EXISTS "${_ort_library}")
    message(FATAL_ERROR "QGC: ONNX Runtime library not found at ${_ort_library}")
endif()

add_library(onnxruntime::onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime::onnxruntime
    PROPERTIES
        IMPORTED_LOCATION "${_ort_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_ort_include_dir}"
)

if(ANDROID)
    set_property(
        TARGET ${CMAKE_PROJECT_NAME}
        APPEND
        PROPERTY
            QT_ANDROID_EXTRA_LIBS "${_ort_library}"
    )
else()
    # The dylib's install name is @rpath-relative and it is never copied into the bundle,
    # so the build-tree binary needs its directory on the rpath.
    set_property(
        TARGET ${CMAKE_PROJECT_NAME}
        APPEND
        PROPERTY
            BUILD_RPATH "${onnxruntime_SOURCE_DIR}/lib"
    )
endif()

message(STATUS "QGC: Person detection enabled (ONNX Runtime ${_ort_version})")
