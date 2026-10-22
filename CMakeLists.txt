cmake_minimum_required(VERSION 3.21)
project(OCTAVE VERSION 0.9.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Sources are UTF-8 (parametercatalog.cpp has unit strings like "°C"): MSVC
# otherwise reads BOM-less files in the system code page
if(MSVC)
    add_compile_options(/utf-8)
endif()
set(CMAKE_AUTOMOC ON)

# ---- Qt 6 ----
set(OCTAVE_QT_COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Network Concurrent SerialPort Bluetooth)
if(ANDROID)
    list(APPEND OCTAVE_QT_COMPONENTS Sensors)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${OCTAVE_QT_COMPONENTS})

# The offline wiki viewer is loaded on demand. Keep builds without a desktop
# web engine working (Qt WebEngine is not available on Android/iOS).
if(NOT ANDROID AND NOT IOS)
    find_package(Qt6 QUIET COMPONENTS WebEngineQuick)
endif()
# On Android the reader uses Qt WebView (the platform's native WebView) and
# reads the wiki from the APK's assets (android/assets/wiki -> wiki/).
if(ANDROID)
    find_package(Qt6 QUIET COMPONENTS WebView)
    if(TARGET Qt6::WebView)
        message(STATUS "Qt WebView found — offline wiki reader enabled on Android")
    else()
        message(WARNING "Qt WebView not found (qtwebview module): the wiki reader will be unavailable in this APK")
    endif()
endif()

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
    # Quick3D's AssetUtils (RuntimeLoader — the music player objects in
    # MediaRoom) links QuickTimeline, which ships in a separate Qt module
    # (qtquicktimeline). The deploy tools bundle only libraries the kit has and
    # the app links, so find and link both explicitly. Without this the v0.9.1
    # APK died at launch ("libQt6QuickTimeline_arm64-v8a.so not found") and the
    # x86_64 AppImage carried the AssetUtils QML plugin without its library.
    find_package(Qt6 QUIET COMPONENTS Quick3DAssetUtils QuickTimeline)
    if(Qt6Quick3DAssetUtils_FOUND AND Qt6QuickTimeline_FOUND)
        message(STATUS "QtQuick3D AssetUtils + QuickTimeline found — music player objects enabled")
    else()
        message(WARNING "QtQuick3D is present but Quick3DAssetUtils/QuickTimeline are not "
                        "(install the qtquicktimeline module): the music player objects "
                        "will be unavailable at runtime and a packaged build may not start.")
    endif()
else()
    message(STATUS "QtQuick3D NOT found — 3D vehicle view will be unavailable at runtime. "
                   "Install it to enable (Arch: qt6-quick3d, Debian/Ubuntu: qt6-quick3d-dev, "
                   "macOS: brew install qt, or add the Quick3D module in the Qt online installer).")
endif()
# SQLite for the black box trip store (src/managers/tripstore.cpp), compiled
# in from the public-domain amalgamation (third_party/sqlite/README.md) so no
# platform depends on a Qt Sql driver plugin being installed at runtime.
add_library(octave_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(octave_sqlite PUBLIC "${CMAKE_SOURCE_DIR}/third_party/sqlite")
target_compile_definitions(octave_sqlite PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_OMIT_DEPRECATED=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=2
    SQLITE_LIKE_DOESNT_MATCH_BLOBS=1
    SQLITE_MAX_EXPR_DEPTH=0)
set_target_properties(octave_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(octave_sqlite PRIVATE -w)   # third-party: not our warnings
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(Threads REQUIRED)
    target_link_libraries(octave_sqlite PUBLIC Threads::Threads)
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
# Release CI passes -DOCTAVE_REQUIRE_FFMPEG=ON so a desktop build can't ship
# without the decoder again (v0.9.4 Windows did, silently).
option(OCTAVE_REQUIRE_FFMPEG "Fail configure when libavcodec/libavutil are missing" OFF)
# Release builds on Linux / macOS link a static, LGPL, H.264-decoder-only
# FFmpeg built by packaging/ffmpeg/build-h264-decoder.sh instead of the
# distro / Homebrew one (GPL, with x264 / x265 / xvid and a dozen codec
# libraries bundled into the package). Empty = use whatever pkg-config finds.
set(OCTAVE_FFMPEG_ROOT "" CACHE PATH "Install prefix of packaging/ffmpeg/build-h264-decoder.sh (static LGPL decoder)")
set(OCTAVE_HAVE_FFMPEG OFF)
if(NOT ANDROID)
    if(OCTAVE_FFMPEG_ROOT)
        if(WIN32)
            message(FATAL_ERROR "OCTAVE_FFMPEG_ROOT is for Linux / macOS; Windows uses vcpkg's ffmpeg[core,avcodec]")
        endif()
        # Its .pc files list -lm / -latomic / -pthread for the static libs; the
        # prefix holds only .a files, so pkg-config resolves to those.
        set(ENV{PKG_CONFIG_PATH} "${OCTAVE_FFMPEG_ROOT}/lib/pkgconfig")
        pkg_check_modules(FFMPEG_DECODER REQUIRED IMPORTED_TARGET libavcodec libavutil)
        set(OCTAVE_HAVE_FFMPEG ON)
    elseif(WIN32)
        # vcpkg's FindFFMPEG wrapper prints the DLLs but leaves FFMPEG_FOUND
        # false, so look the headers and import libs up directly (vcpkg's
        # toolchain puts installed/<triplet>/{include,lib} on the search path).
        find_path(FFMPEG_INCLUDE_DIRS libavcodec/avcodec.h)
        find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec)
        find_library(FFMPEG_AVUTIL_LIBRARY NAMES avutil)
        if(FFMPEG_INCLUDE_DIRS AND FFMPEG_AVCODEC_LIBRARY AND FFMPEG_AVUTIL_LIBRARY)
            set(FFMPEG_LIBRARIES ${FFMPEG_AVCODEC_LIBRARY} ${FFMPEG_AVUTIL_LIBRARY})
            set(OCTAVE_HAVE_FFMPEG ON)
        endif()
    else()
        pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavutil)
        if(FFMPEG_FOUND)
            set(OCTAVE_HAVE_FFMPEG ON)
        endif()
    endif()
endif()
if(OCTAVE_FFMPEG_ROOT)
    message(STATUS "Built-in phone mirror decoder (libavcodec): ${OCTAVE_HAVE_FFMPEG} (static, ${OCTAVE_FFMPEG_ROOT})")
else()
    message(STATUS "Built-in phone mirror decoder (libavcodec): ${OCTAVE_HAVE_FFMPEG}")
endif()
if(OCTAVE_REQUIRE_FFMPEG AND NOT OCTAVE_HAVE_FFMPEG)
    message(FATAL_ERROR "OCTAVE_REQUIRE_FFMPEG is ON but libavcodec/libavutil were not found "
                        "(Windows: vcpkg install ffmpeg[core,avcodec]; Debian/Ubuntu: libavcodec-dev libavutil-dev; "
                        "macOS: brew install ffmpeg)")
endif()

# ---- Download feature flag ----
# Desktop: default ON. Disable with `-DOCTAVE_ENABLE_DOWNLOADS=OFF` for app-store builds
#          (yt-dlp is forbidden on Apple/Microsoft/Play stores).
# Android: default ON for sideload APKs — downloads run through the
#          youtubedl-android AAR (Python + yt-dlp + ffmpeg bundled). For Play
#          Store builds, pass `-DOCTAVE_ENABLE_DOWNLOADS=OFF` explicitly.
option(OCTAVE_ENABLE_DOWNLOADS "Enable music download feature (disable for app store builds)" ON)

# ---- Developer tooling (src/dev/) ----
# `octave --dev` (simulated car over a fake ELM327, dev overlay) and
# `octave --profile` (perf monitor + JSON command server on 127.0.0.1:9876,
# which the dev/ MCP servers and screenshot scripts drive). Both are inactive
# without their flag. Default ON only for desktop Debug builds; anything else
# (Release-type, no build type -- the head unit's build dir, multi-config
# generators -- Android, iOS) gets OFF unless asked for, so shipped binaries
# never contain it.
if(NOT ANDROID AND NOT IOS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_octave_dev_tools_default ON)
else()
    set(_octave_dev_tools_default OFF)
endif()
option(OCTAVE_DEV_TOOLS "Build the --dev / --profile developer tooling (src/dev/)" ${_octave_dev_tools_default})
message(STATUS "Developer tooling (--dev / --profile): ${OCTAVE_DEV_TOOLS}")

# ---- Executable ----
qt_add_executable(octave
    src/main.cpp
    src/util/logger.h
    src/util/logger.cpp
    src/util/assetstore.cpp
    src/util/assetstore.h
    src/util/uiwatchdog.h
    src/util/uiwatchdog.cpp
    src/util/perfscope.h
    src/util/dashboardgrid.h
    src/util/dashboardgrid.cpp
    src/util/qmlmodules.h
    src/util/qmlmodules.cpp
    src/managers/settingsmanager.h
    src/managers/settingsmanager.cpp
    src/managers/diagnosticsmanager.h
    src/managers/diagnosticsmanager.cpp
    src/managers/clock.h
    src/managers/clock.cpp
    src/managers/networkmanager.h
    src/managers/networkmanager.cpp
    src/managers/volumecontroller.h
    src/managers/volumecontroller.cpp
    src/managers/parametercatalog.h
    src/managers/parametercatalog.cpp
    src/managers/parameterstore.h
    src/managers/parameterstore.cpp
    src/managers/nowplaying.h
    src/managers/nowplaying.cpp
    src/managers/shiftlightmanager.h
    src/managers/shiftlightmanager.cpp
    src/managers/mediamanager.h
    src/managers/mediamanager.cpp
    src/managers/audioanalyzer.h
    src/managers/audioanalyzer.cpp
    src/managers/downloadmanager.h
    src/managers/downloadmanager.cpp
    src/managers/downloadutil.h
    src/managers/downloadutil.cpp
    src/managers/elm327protocol.h
    src/managers/elm327protocol.cpp
    src/managers/obdmanager.h
    src/managers/obdmanager.cpp
    src/managers/rfcommsocket.h
    src/managers/rfcommsocket.cpp
    src/managers/obdadapterfinder.h
    src/managers/obdadapterfinder.cpp
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
    src/managers/tripstore.h
    src/managers/tripstore.cpp
    src/managers/triprecorder.h
    src/managers/triprecorder.cpp
    src/phone_mirror/scrcpyclient.h
    src/phone_mirror/adaptivebitrate.h
    src/phone_mirror/scrcpyclient.cpp
    src/phone_mirror/qrcodegen.hpp
    src/phone_mirror/qrcodegen.cpp
    src/items/embeddeddhuitem.h
    src/items/embeddeddhuitem.cpp
    # Android-only platform headers: fully guarded by #ifdef Q_OS_ANDROID
    # so AUTOMOC on desktop produces empty moc files (no Q_OBJECT visible).
    src/platform/androidmediabridge.h
    src/platform/androidprocessadapter.h
)

# Commit hash and date baked in at configure time. Packaged builds (APK,
# installers, AppImage) have no .git and no git binary, so the About page
# showed "Commit: unknown" on the phone; SettingsManager falls back to these
# when running git fails. Reconfigure is triggered by a change of HEAD/index.
set(OCTAVE_GIT_COMMIT "unknown")
set(OCTAVE_GIT_DATE "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _git_hash
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _git_rc)
    if(_git_rc EQUAL 0 AND NOT _git_hash STREQUAL "")
        set(OCTAVE_GIT_COMMIT "${_git_hash}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" log -1 --format=%ci
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _git_date
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _git_rc)
    if(_git_rc EQUAL 0 AND NOT _git_date STREQUAL "")
        set(OCTAVE_GIT_DATE "${_git_date}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/.git/HEAD" "${CMAKE_SOURCE_DIR}/.git/index")
endif()
message(STATUS "Build commit: ${OCTAVE_GIT_COMMIT} (${OCTAVE_GIT_DATE})")
# A generated header, not compile definitions: a changed definition rebuilds
# every source file, which made each self-update on the Pi a full rebuild.
# configure_file only rewrites the header when the values change, so a new
# commit recompiles just the files that include it.
configure_file(src/util/buildinfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/buildinfo.h" @ONLY)
target_include_directories(octave PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_definitions(octave PRIVATE
    OCTAVE_VERSION="${PROJECT_VERSION}"
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
    Qt6::SerialPort
    Qt6::Bluetooth
    octave_sqlite
)
# Desktop Linux pairs OBD adapters through BlueZ over D-Bus (PIN agent +
# trust), see src/platform/bluezpairer.h. Qt Bluetooth already depends on
# Qt DBus there, so this adds no new runtime library.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_sources(octave PRIVATE
        src/platform/bluezpairer.h
        src/platform/bluezpairer.cpp
    )
    target_link_libraries(octave PRIVATE Qt6::DBus)
    target_compile_definitions(octave PRIVATE OCTAVE_BLUEZ)
endif()
if(OCTAVE_HAVE_FFMPEG)
    if(OCTAVE_FFMPEG_ROOT)
        target_link_libraries(octave PRIVATE PkgConfig::FFMPEG_DECODER)
        # Keep the static avcodec_* / av_* symbols out of the dynamic symbol
        # table: on aarch64 Qt's media plugin loads the distro's shared FFmpeg
        # into the same process, and must not bind to ours.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_options(octave PRIVATE "LINKER:--exclude-libs,libavcodec.a:libavutil.a")
        endif()
    elseif(WIN32)
        target_include_directories(octave PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_libraries(octave PRIVATE ${FFMPEG_LIBRARIES})
    else()
        target_link_libraries(octave PRIVATE PkgConfig::FFMPEG)
    endif()
endif()
if(Qt6Quick3D_FOUND)
    target_link_libraries(octave PRIVATE Qt6::Quick3D)
    # main.cpp: a mobile package always bundles a linked module (hasQuick3D)
    target_compile_definitions(octave PRIVATE OCTAVE_LINKED_QUICK3D)
    if(Qt6Quick3DAssetUtils_FOUND AND Qt6QuickTimeline_FOUND)
        target_link_libraries(octave PRIVATE Qt6::Quick3DAssetUtils Qt6::QuickTimeline)
        target_compile_definitions(octave PRIVATE OCTAVE_LINKED_QUICK3D_ASSETUTILS)
    endif()
endif()
if(TARGET Qt6::WebEngineQuick)
    target_link_libraries(octave PRIVATE Qt6::WebEngineQuick)
    target_compile_definitions(octave PRIVATE OCTAVE_HAVE_WEBENGINE)
    # Installed apps have an offline copy; source launches prefer the live wiki/.
    file(GLOB_RECURSE OCTAVE_WIKI_FILES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/wiki/*.html" "${CMAKE_SOURCE_DIR}/wiki/*.css"
        "${CMAKE_SOURCE_DIR}/wiki/*.js" "${CMAKE_SOURCE_DIR}/wiki/*.svg"
        "${CMAKE_SOURCE_DIR}/wiki/*.png" "${CMAKE_SOURCE_DIR}/wiki/*.webp")
    qt_add_resources(octave "wiki" PREFIX "/wiki"
        BASE "${CMAKE_SOURCE_DIR}/wiki" FILES ${OCTAVE_WIKI_FILES})
endif()
if(ANDROID AND TARGET Qt6::WebView)
    target_link_libraries(octave PRIVATE Qt6::WebView)
    target_compile_definitions(octave PRIVATE OCTAVE_HAVE_WEBVIEW)
endif()
if(ANDROID)
    target_link_libraries(octave PRIVATE Qt6::Sensors Qt6::CorePrivate)
elseif(WIN32)
    target_link_libraries(octave PRIVATE TagLib::tag)
else()
    target_link_libraries(octave PRIVATE PkgConfig::TAGLIB)
endif()

if(OCTAVE_DEV_TOOLS)
    # QTest drives synthetic mouse input (the click / drag / long_press commands)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_sources(octave PRIVATE
        src/dev/devruntime.h
        src/dev/devruntime.cpp
        src/dev/commandserver.h
        src/dev/commandserver.cpp
        src/dev/coreactions.h
        src/dev/coreactions.cpp
        src/dev/uidriver.h
        src/dev/uidriver.cpp
        src/dev/perfmonitor.h
        src/dev/perfmonitor.cpp
        src/dev/carsim.h
        src/dev/carsim.cpp
        src/dev/simelm327.h
        src/dev/simelm327.cpp
        src/dev/devimufeed.h
        src/dev/devimufeed.cpp
        src/dev/devtoolsmanager.h
        src/dev/devtoolsmanager.cpp
        src/dev/collagebridge.h
        src/dev/collagebridge.cpp
    )
    target_link_libraries(octave PRIVATE Qt6::Test)
    target_compile_definitions(octave PRIVATE OCTAVE_DEV_TOOLS)
endif()

# ---- C++ tests (tests/cpp/, run with ctest) ----
# Same default as the dev tooling: ON for desktop Debug builds, OFF otherwise.
if(NOT ANDROID AND NOT IOS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_octave_tests_default ON)
else()
    set(_octave_tests_default OFF)
endif()
option(OCTAVE_BUILD_TESTS "Build the QtTest suite in tests/cpp (ctest)" ${_octave_tests_default})
message(STATUS "C++ tests (ctest): ${OCTAVE_BUILD_TESTS}")
if(OCTAVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/cpp)
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

# OCTAVE's license and third-party notice, for Settings > About > licenses
# (DiagnosticsManager::licenseText); the frontend/ folder doesn't carry them.
qt_add_resources(octave "licenses"
    PREFIX "/licenses"
    BASE "${CMAKE_SOURCE_DIR}"
    FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/NOTICE"
)

# ---- QML import path: reuse existing frontend/ directory ----
# The QML files live in frontend/ and are loaded at runtime (not compiled into resources).
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
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.robdegeorge.octave"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# ---- Windows: GUI subsystem ----
# Without this OCTAVE.exe is a console program and a black console window
# opens behind the app on every launch. Logging goes to rotating files
# (src/util/logger.cpp), so nothing needs stdout; Qt's entry-point library
# supplies WinMain, and QProcess children (adb, yt-dlp) get CREATE_NO_WINDOW
# automatically when the parent has no console.
if(WIN32)
    set_target_properties(octave PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ---- Android packaging ----
if(ANDROID)
    # androiddeployqt packages the app, bundles Qt .so's, generates the APK.
    # Properties must be set before the auto-finalize step that qt_add_executable
    # schedules (via CMake's DEFERRED mechanism).
    # The APK's version follows project(): versionName is the version string,
    # versionCode is major*10000 + minor*100 + patch (0.9.5 -> 905). Android
    # only installs an APK whose code is higher than the installed one, so it
    # must keep rising; it was hand-edited up to 6 before this.
    math(EXPR OCTAVE_ANDROID_VERSION_CODE
         "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
    set_target_properties(octave PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
        QT_ANDROID_VERSION_CODE ${OCTAVE_ANDROID_VERSION_CODE}
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        # Capped at 33 by AGP 7.4.1 (Qt 6.7.3's target AGP).
        # Bump to 34+ once upgrading to Qt 6.8+ + AGP 8.5+ (Play Store target).
        QT_ANDROID_TARGET_SDK_VERSION 33
        QT_ANDROID_MIN_SDK_VERSION 30
        QT_ANDROID_ABIS "arm64-v8a"
        # Bundle the QML frontend/ dir into the APK so runtime-loaded QML is findable.
        QT_QML_ROOT_PATH "${CMAKE_SOURCE_DIR}/frontend"
    )
    # Qt for Android ships the OpenSSL TLS *plugin* but not OpenSSL itself;
    # without libcrypto_3/libssl_3 in the APK every QSslSocket fails with "No
    # functional TLS backend was found" (self-update, any Qt-side
    # HTTPS). Prebuilt from KDAB/android_openssl — see android/openssl/README.md.
    foreach(abi IN LISTS QT_ANDROID_ABIS)
        set(_ssl_dir "${CMAKE_SOURCE_DIR}/android/openssl/${abi}")
        if(EXISTS "${_ssl_dir}/libssl_3.so")
            set_property(TARGET octave APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
                "${_ssl_dir}/libcrypto_3.so" "${_ssl_dir}/libssl_3.so")
        else()
            message(WARNING "android/openssl/${abi}: no OpenSSL libraries — Qt HTTPS will not work in this APK")
        endif()
    endforeach()
    # androiddeployqt only bundles plugin categories the linked Qt modules
    # declare, and Quick3D's "assetimporters" (the Assimp glTF importer behind
    # RuntimeLoader) is not one of them: v0.9.2 shipped an APK where every glb
    # failed with "Unsupported: Unknown error" (music player objects, the Jeep).
    if(Qt6Quick3D_FOUND)
        set_property(TARGET octave APPEND PROPERTY QT_ANDROID_EXTRA_PLUGINS
            "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/assetimporters")
    endif()
    # android/build.gradle reads this to keep the youtubedl-android AAR, the
    # Java download bridge, and the yt-dlp assets out of store APKs. Gradle
    # runs in android-build*/ directly under this dir and looks one level up.
    if(OCTAVE_ENABLE_DOWNLOADS)
        set(_octave_gradle_downloads true)
    else()
        set(_octave_gradle_downloads false)
    endif()
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/octave-android-features.properties"
         CONTENT "octaveEnableDownloads=${_octave_gradle_downloads}\n")
    # No manual qt_finalize_executable call — qt_add_executable schedules it automatically.
endif()
