cmake_minimum_required(VERSION 3.21)
project(OCTAVE VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---- Qt 6 ----
set(OCTAVE_QT_COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Network Concurrent NetworkAuth SerialPort Bluetooth)
if(ANDROID)
    list(APPEND OCTAVE_QT_COMPONENTS Sensors)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${OCTAVE_QT_COMPONENTS})

# Quick3D backs the 3D vehicle-attitude view (frontend/CarMenu.qml). It is
# optional at build time: the QML is loaded lazily and SensorHome probes for the
# module, greying out the "3D View" card when it is absent. Linking it when
# present is what makes the deploy tools (windeployqt / macdeployqt /
# androiddeployqt / linuxdeploy) bundle the QML module into a package.
find_package(Qt6 QUIET COMPONENTS Quick3D)
if(Qt6Quick3D_FOUND)
    message(STATUS "QtQuick3D found — 3D vehicle view enabled")
    # Record the plugin root of the Qt that CMake actually selected. The AppImage
    # packaging script needs the "assetimporters" plugin dir (linuxdeploy-plugin-qt
    # doesn't know that category) and cannot reliably re-derive it from PATH, where
    # qmake6 may well resolve to an Android kit instead of this desktop one.
    file(WRITE "${CMAKE_BINARY_DIR}/qt_plugins_path.txt"
         "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
else()
    message(STATUS "QtQuick3D NOT found — 3D vehicle view will be unavailable at runtime. "
                   "Install it to enable (Arch: qt6-quick3d, Debian/Ubuntu: qt6-quick3d-dev, "
                   "macOS: brew install qt, or add the Quick3D module in the Qt online installer).")
endif()
# Qt6::CorePrivate is an imported target provided by Qt6::Core — not a
# separately-findable component. Linking it grants access to private headers
# (QtAndroidPrivate::requestPermission on Android).

# TagLib only available on desktop (no pkg-config on Android host; mobile uses Qt metadata fallback).
# Windows: vcpkg ships taglib's CMake config — find_package is the supported path there.
# Linux/macOS: pkg-config remains the easiest route (apt's libtag1-dev / Homebrew's taglib both ship .pc files).
if(NOT ANDROID)
    if(WIN32)
        find_package(taglib CONFIG REQUIRED)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
    endif()
endif()

# ---- FFmpeg (libavcodec) for the built-in phone-mirror client ----
# Optional: without it the C++ backend still builds and reports
# phoneMirrorManager.nativeAvailable=false (docs/PHONE_MIRROR_NATIVE_PLAN.md).
set(OCTAVE_HAVE_FFMPEG OFF)
if(NOT ANDROID)
    if(WIN32)
        find_package(FFMPEG QUIET)               # vcpkg: ffmpeg[avcodec,avutil]
        if(FFMPEG_FOUND)
            set(OCTAVE_HAVE_FFMPEG ON)
        endif()
    else()
        pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavutil)
        if(FFMPEG_FOUND)
            set(OCTAVE_HAVE_FFMPEG ON)
        endif()
    endif()
endif()
message(STATUS "Built-in phone mirror decoder (libavcodec): ${OCTAVE_HAVE_FFMPEG}")

# ---- Download feature flag ----
# Desktop: default ON. Disable with `-DOCTAVE_ENABLE_DOWNLOADS=OFF` for app-store builds
#          (yt-dlp is forbidden on Apple/Microsoft/Play stores).
# Android: default ON for sideload APKs — downloads run through the
#          youtubedl-android AAR (Python + yt-dlp + ffmpeg bundled). For Play
#          Store builds, pass `-DOCTAVE_ENABLE_DOWNLOADS=OFF` explicitly.
option(OCTAVE_ENABLE_DOWNLOADS "Enable music download feature (disable for app store builds)" ON)

# ---- Executable ----
qt_add_executable(octave
    src/main.cpp
    src/managers/settingsmanager.h
    src/managers/settingsmanager.cpp
    src/managers/clock.h
    src/managers/clock.cpp
    src/managers/networkmanager.h
    src/managers/networkmanager.cpp
    src/managers/volumecontroller.h
    src/managers/volumecontroller.cpp
    src/managers/mediamanager.h
    src/managers/mediamanager.cpp
    src/managers/audioanalyzer.h
    src/managers/audioanalyzer.cpp
    src/managers/spotifymanager.h
    src/managers/spotifymanager.cpp
    src/managers/downloadmanager.h
    src/managers/downloadmanager.cpp
    src/managers/elm327protocol.h
    src/managers/elm327protocol.cpp
    src/managers/obdmanager.h
    src/managers/obdmanager.cpp
    src/managers/esp32volumemanager.h
    src/managers/esp32volumemanager.cpp
    src/managers/berryimumanager.h
    src/managers/berryimumanager.cpp
    src/managers/gesturemanager.h
    src/managers/gesturemanager.cpp
    src/managers/phonemirrormanager.h
    src/managers/phonemirrormanager.cpp
    src/managers/androidautomanager.h
    src/managers/androidautomanager.cpp
    src/managers/dashboardmanager.h
    src/managers/dashboardmanager.cpp
    src/phone_mirror/scrcpyclient.h
    src/phone_mirror/scrcpyclient.cpp
    src/items/embeddeddhuitem.h
    src/items/embeddeddhuitem.cpp
    # Android-only platform headers: fully guarded by #ifdef Q_OS_ANDROID
    # so AUTOMOC on desktop produces empty moc files (no Q_OBJECT visible).
    src/platform/androidmediabridge.h
    src/platform/androidprocessadapter.h
)

target_compile_definitions(octave PRIVATE
    $<$<BOOL:${OCTAVE_ENABLE_DOWNLOADS}>:OCTAVE_ENABLE_DOWNLOADS>
    $<$<BOOL:${OCTAVE_HAVE_FFMPEG}>:OCTAVE_HAVE_FFMPEG>
    # Activate existing #ifndef Q_OS_MOBILE guards on Android (and iOS later).
    # Without this define the guards are no-ops and desktop-only code compiles on mobile.
    $<$<BOOL:${ANDROID}>:Q_OS_MOBILE>
)

target_link_libraries(octave PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Network
    Qt6::Concurrent
    Qt6::NetworkAuth
    Qt6::SerialPort
    Qt6::Bluetooth
)
if(OCTAVE_HAVE_FFMPEG)
    if(WIN32)
        target_include_directories(octave PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_libraries(octave PRIVATE ${FFMPEG_LIBRARIES})
    else()
        target_link_libraries(octave PRIVATE PkgConfig::FFMPEG)
    endif()
endif()
if(Qt6Quick3D_FOUND)
    target_link_libraries(octave PRIVATE Qt6::Quick3D)
endif()
if(ANDROID)
    target_link_libraries(octave PRIVATE Qt6::Sensors Qt6::CorePrivate)
elseif(WIN32)
    target_link_libraries(octave PRIVATE TagLib::tag)
else()
    target_link_libraries(octave PRIVATE PkgConfig::TAGLIB)
endif()

# ---- Shader resources ----
# Qt RHI's ShaderEffect rejects assets:/ URIs on Android (only qrc:/ or local files),
# so .qsb shaders must live in the compiled-in Qt resource system. QML branches on
# Qt.platform.os to use qrc:/ on Android and filesystem on desktop.
qt_add_resources(octave "shaders"
    PREFIX "/shaders"
    BASE "${CMAKE_SOURCE_DIR}/frontend/shaders"
    FILES "${CMAKE_SOURCE_DIR}/frontend/shaders/dissolve_squares.frag.qsb"
)

# ---- Bundled phone server (OCTAVE's fork of the scrcpy server, Apache-2.0) ----
# Source in phone_server/; prebuilt jar in tools/phone-server/ (rebuilt and
# checked by the phone-server CI job). Embedded as a resource and written to a
# temp file at runtime, so no packaging target has to carry it as a loose file.
qt_add_resources(octave "phone_server"
    PREFIX "/phone-server"
    BASE "${CMAKE_SOURCE_DIR}/tools/phone-server"
    FILES "${CMAKE_SOURCE_DIR}/tools/phone-server/octave-phone-server"
          "${CMAKE_SOURCE_DIR}/tools/phone-server/LICENSE"
          "${CMAKE_SOURCE_DIR}/tools/phone-server/NOTICE"
)

# ---- QML import path: reuse existing frontend/ directory ----
# The QML files live in frontend/ and are loaded at runtime (not compiled into resources).
# This keeps them shared with the Python backend during migration.
set_target_properties(octave PROPERTIES
    QT_QML_IMPORT_PATH "${CMAKE_SOURCE_DIR}/frontend"
)

# ---- macOS bundle ----
# qt_add_executable does not auto-set MACOSX_BUNDLE; without this, the macOS
# build emits a plain `octave` ELF binary instead of `octave.app`, and
# macdeployqt has nothing to bundle. iOS would also need this once enabled.
if(APPLE)
    set_target_properties(octave PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "OCTAVE"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.waybettersolutions.octave"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# ---- Android packaging ----
if(ANDROID)
    # androiddeployqt packages the app, bundles Qt .so's, generates the APK.
    # Properties must be set before the auto-finalize step that qt_add_executable
    # schedules (via CMake's DEFERRED mechanism).
    set_target_properties(octave PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
        # Capped at 33 by AGP 7.4.1 (Qt 6.7.3's target AGP).
        # Bump to 34+ once upgrading to Qt 6.8+ + AGP 8.5+ (Play Store target).
        QT_ANDROID_TARGET_SDK_VERSION 33
        QT_ANDROID_MIN_SDK_VERSION 30
        QT_ANDROID_ABIS "arm64-v8a"
        # Bundle the QML frontend/ dir into the APK so runtime-loaded QML is findable.
        QT_QML_ROOT_PATH "${CMAKE_SOURCE_DIR}/frontend"
    )
    # No manual qt_finalize_executable call — qt_add_executable schedules it automatically.
endif()
