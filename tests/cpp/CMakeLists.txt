# QtTest suite for the C++ backend. Each test compiles the sources it needs
# (no shared library of the managers yet), runs headless on the offscreen
# platform, and is registered with ctest:
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # OCTAVE_BUILD_TESTS defaults ON
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure

find_package(Qt6 REQUIRED COMPONENTS Test)

set(OCTAVE_SRC "${CMAKE_SOURCE_DIR}/src")

# octave_add_test(<name> [SOURCES <src/... relative>...] [LIBS <targets>...])
# builds <name>.cpp plus SOURCES into a test executable and registers it.
function(octave_add_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS;DEFINES" ${ARGN})
    set(srcs "${name}.cpp")
    foreach(s IN LISTS T_SOURCES)
        list(APPEND srcs "${OCTAVE_SRC}/${s}")
    endforeach()
    add_executable(${name} ${srcs})
    target_include_directories(${name} PRIVATE "${OCTAVE_SRC}" "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Core ${T_LIBS})
    target_compile_definitions(${name} PRIVATE
        OCTAVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        OCTAVE_VERSION="${PROJECT_VERSION}"
        ${T_DEFINES})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
        TIMEOUT 120)
endfunction()

octave_add_test(tst_elm327protocol SOURCES managers/elm327protocol.cpp managers/parametercatalog.h managers/parametercatalog.cpp)
octave_add_test(tst_adaptivebitrate)
octave_add_test(tst_settingsmanager SOURCES managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui)
octave_add_test(tst_volumecontroller SOURCES managers/volumecontroller.h managers/volumecontroller.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui)
octave_add_test(tst_shiftlightmanager SOURCES managers/shiftlightmanager.h managers/shiftlightmanager.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui)
octave_add_test(tst_parametercatalog
    SOURCES managers/parametercatalog.h managers/parametercatalog.cpp managers/elm327protocol.cpp
    LIBS Qt6::Gui Qt6::Qml)
octave_add_test(tst_parameterstore
    SOURCES managers/parameterstore.h managers/parameterstore.cpp managers/parametercatalog.h managers/parametercatalog.cpp
    LIBS Qt6::Gui Qt6::Qml)
octave_add_test(tst_dashboardmanager SOURCES managers/dashboardmanager.h managers/dashboardmanager.cpp
                util/dashboardgrid.h util/dashboardgrid.cpp LIBS Qt6::Gui)
octave_add_test(tst_triprecorder
    SOURCES managers/tripstore.h managers/tripstore.cpp managers/triprecorder.h managers/triprecorder.cpp
            managers/obdmanager.h managers/obdmanager.cpp managers/parametercatalog.h managers/parametercatalog.cpp
            managers/elm327protocol.cpp managers/rfcommsocket.h managers/rfcommsocket.cpp
            managers/settingsmanager.h managers/settingsmanager.cpp
    LIBS Qt6::Gui Qt6::SerialPort octave_sqlite)
octave_add_test(tst_esp32readbuffer SOURCES managers/esp32volumemanager.h managers/esp32volumemanager.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui Qt6::SerialPort)
# Port selection and the no-writes-before-the-knob-speaks rule, over a pty (Linux)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    octave_add_test(tst_esp32volumemanager SOURCES managers/esp32volumemanager.h managers/esp32volumemanager.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui Qt6::SerialPort)
endif()
# The IMU / gesture managers follow their settings (desktop Linux builds use I2C)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    octave_add_test(tst_sensorsettings
        SOURCES managers/berryimumanager.h managers/berryimumanager.cpp
                managers/gesturemanager.h managers/gesturemanager.cpp
                managers/settingsmanager.h managers/settingsmanager.cpp
        LIBS Qt6::Gui)
endif()
octave_add_test(tst_downloadutil SOURCES managers/downloadutil.cpp)
octave_add_test(tst_obdmanager
    SOURCES managers/obdmanager.h managers/obdmanager.cpp managers/parametercatalog.h managers/parametercatalog.cpp
            managers/elm327protocol.cpp managers/rfcommsocket.h managers/rfcommsocket.cpp
            managers/settingsmanager.h managers/settingsmanager.cpp
    LIBS Qt6::Gui Qt6::SerialPort)

# Integration: the real OBD stack against the simulated adapter (src/dev/)
if(OCTAVE_DEV_TOOLS)
    octave_add_test(tst_obdsim
        SOURCES managers/obdmanager.h managers/obdmanager.cpp managers/parametercatalog.h managers/parametercatalog.cpp
                managers/elm327protocol.cpp managers/rfcommsocket.h managers/rfcommsocket.cpp
                managers/settingsmanager.h managers/settingsmanager.cpp
                dev/carsim.h dev/carsim.cpp dev/simelm327.h dev/simelm327.cpp
        LIBS Qt6::Gui Qt6::SerialPort)
endif()

# The adapter finder's hand-off connects to the simulated adapter (src/dev/)
if(OCTAVE_DEV_TOOLS)
    set(_finder_sources managers/obdadapterfinder.h managers/obdadapterfinder.cpp)
    set(_finder_libs Qt6::Gui Qt6::SerialPort Qt6::Bluetooth)
    set(_finder_defines)
    if(TARGET Qt6::DBus)   # Linux: the BlueZ pairing agent, as in the app
        list(APPEND _finder_sources platform/bluezpairer.h platform/bluezpairer.cpp)
        list(APPEND _finder_libs Qt6::DBus)
        list(APPEND _finder_defines OCTAVE_BLUEZ)
    endif()
    octave_add_test(tst_obdadapterfinder
        SOURCES ${_finder_sources}
                managers/obdmanager.h managers/obdmanager.cpp managers/parametercatalog.h managers/parametercatalog.cpp
                managers/elm327protocol.cpp managers/rfcommsocket.h managers/rfcommsocket.cpp
                managers/settingsmanager.h managers/settingsmanager.cpp
                dev/carsim.h dev/carsim.cpp dev/simelm327.h dev/simelm327.cpp
        LIBS ${_finder_libs}
        DEFINES ${_finder_defines})
endif()

# MediaManager reads tags with TagLib, as the app does
if(TARGET TagLib::tag)
    set(_taglib TagLib::tag)
else()
    set(_taglib PkgConfig::TAGLIB)
endif()
octave_add_test(tst_downloadmanager
    SOURCES managers/downloadmanager.h managers/downloadmanager.cpp
            managers/downloadutil.h managers/downloadutil.cpp
            managers/settingsmanager.h managers/settingsmanager.cpp
            util/variantlistmodel.h util/variantlistmodel.cpp
    LIBS Qt6::Gui Qt6::Network Qt6::Concurrent ${_taglib}
    DEFINES OCTAVE_ENABLE_DOWNLOADS)
octave_add_test(tst_mediamanager
    SOURCES managers/mediamanager.h managers/mediamanager.cpp util/units.h util/units.cpp
            managers/settingsmanager.h managers/settingsmanager.cpp
    LIBS Qt6::Gui Qt6::Multimedia Qt6::Concurrent ${_taglib})

octave_add_test(tst_musicdeviceassets)
octave_add_test(tst_vehiclegate SOURCES managers/themecatalog.h managers/themecatalog.cpp managers/dashboardmanager.h managers/dashboardmanager.cpp util/dashboardgrid.h util/dashboardgrid.cpp managers/parametercatalog.h managers/parametercatalog.cpp managers/parameterstore.h managers/parameterstore.cpp LIBS Qt6::Gui Qt6::Qml Qt6::Quick)

octave_add_test(tst_phonemirrorview SOURCES managers/themecatalog.h managers/themecatalog.cpp LIBS Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Multimedia)
octave_add_test(tst_errortoast SOURCES managers/themecatalog.h managers/themecatalog.cpp LIBS Qt6::Gui Qt6::Qml Qt6::Quick)
# The theme table as data and the palette in force
octave_add_test(tst_themecatalog SOURCES managers/themecatalog.h managers/themecatalog.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui)

# QML fixtures (tests/fixtures/), each run in its own qmlfixturehost process
add_executable(qmlfixturehost qmlfixturehost.cpp
    "${OCTAVE_SRC}/managers/settingsmanager.h" "${OCTAVE_SRC}/managers/settingsmanager.cpp"
    "${OCTAVE_SRC}/util/assetstore.h" "${OCTAVE_SRC}/util/assetstore.cpp"
    "${OCTAVE_SRC}/util/units.h" "${OCTAVE_SRC}/util/units.cpp"
    "${OCTAVE_SRC}/managers/themecatalog.h" "${OCTAVE_SRC}/managers/themecatalog.cpp")
target_include_directories(qmlfixturehost PRIVATE "${OCTAVE_SRC}" "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(qmlfixturehost PRIVATE OCTAVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}" OCTAVE_VERSION="${PROJECT_VERSION}")
target_link_libraries(qmlfixturehost PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
set(_fixture_defines OCTAVE_QML_FIXTURE_HOST="$<TARGET_FILE:qmlfixturehost>")
if(TARGET Qt6::WebEngineQuick)
    target_link_libraries(qmlfixturehost PRIVATE Qt6::WebEngineQuick)
    target_compile_definitions(qmlfixturehost PRIVATE OCTAVE_HAVE_WEBENGINE)
    list(APPEND _fixture_defines OCTAVE_HAVE_WEBENGINE)
endif()
octave_add_test(tst_qmlfixtures DEFINES ${_fixture_defines})
add_dependencies(tst_qmlfixtures qmlfixturehost)
set_tests_properties(tst_qmlfixtures PROPERTIES TIMEOUT 300)

# Phone mirror: the client (and the manager on top) with tests/cpp/fake-adb.sh
# as adb; the decoding tests need libavcodec, as the app does
set(_mirror_libs Qt6::Gui Qt6::Multimedia Qt6::Network)
set(_mirror_defines)
if(OCTAVE_HAVE_FFMPEG)
    if(WIN32)
        list(APPEND _mirror_libs ${FFMPEG_LIBRARIES})
    else()
        list(APPEND _mirror_libs PkgConfig::FFMPEG)
    endif()
    list(APPEND _mirror_defines OCTAVE_HAVE_FFMPEG)
endif()
octave_add_test(tst_scrcpyclient
    SOURCES phone_mirror/scrcpyclient.h phone_mirror/scrcpyclient.cpp phone_mirror/adaptivebitrate.h
    LIBS ${_mirror_libs}
    DEFINES ${_mirror_defines})
if(WIN32 AND OCTAVE_HAVE_FFMPEG)
    target_include_directories(tst_scrcpyclient PRIVATE ${FFMPEG_INCLUDE_DIRS})
endif()
octave_add_test(tst_phonemirror
    SOURCES managers/phonemirrormanager.h managers/phonemirrormanager.cpp
            phone_mirror/scrcpyclient.h phone_mirror/scrcpyclient.cpp phone_mirror/adaptivebitrate.h
            phone_mirror/qrcodegen.hpp phone_mirror/qrcodegen.cpp
            managers/settingsmanager.h managers/settingsmanager.cpp
    LIBS ${_mirror_libs}
    DEFINES ${_mirror_defines})
if(WIN32 AND OCTAVE_HAVE_FFMPEG)
    target_include_directories(tst_phonemirror PRIVATE ${FFMPEG_INCLUDE_DIRS})
endif()
# NowPlaying over the real MediaManager and PhoneMirrorManager
octave_add_test(tst_nowplaying
    SOURCES managers/nowplaying.h managers/nowplaying.cpp
            managers/mediamanager.h managers/mediamanager.cpp util/units.h util/units.cpp
            managers/phonemirrormanager.h managers/phonemirrormanager.cpp
            phone_mirror/scrcpyclient.h phone_mirror/scrcpyclient.cpp phone_mirror/adaptivebitrate.h
            phone_mirror/qrcodegen.hpp phone_mirror/qrcodegen.cpp
            managers/settingsmanager.h managers/settingsmanager.cpp
    LIBS ${_mirror_libs} Qt6::Concurrent ${_taglib}
    DEFINES ${_mirror_defines})
if(WIN32 AND OCTAVE_HAVE_FFMPEG)
    target_include_directories(tst_nowplaying PRIVATE ${FFMPEG_INCLUDE_DIRS})
endif()
# The bundled phone server, as in the app (ScrcpyClient::available() checks it)
qt_add_resources(tst_phonemirror "phone_server"
    PREFIX "/phone-server"
    BASE "${CMAKE_SOURCE_DIR}/tools/phone-server"
    FILES "${CMAKE_SOURCE_DIR}/tools/phone-server/octave-phone-server")

# The whole app boots (--smoke), and with the dev tooling --dev --profile too
set(_boot_defines OCTAVE_APP_BINARY="$<TARGET_FILE:octave>")
if(OCTAVE_DEV_TOOLS)
    list(APPEND _boot_defines OCTAVE_DEV_TOOLS)
endif()
octave_add_test(tst_boot LIBS Qt6::Network DEFINES ${_boot_defines})
add_dependencies(tst_boot octave)
set_tests_properties(tst_boot PROPERTIES TIMEOUT 180)

octave_add_test(tst_audioanalyzer SOURCES managers/audioanalyzer.h managers/audioanalyzer.cpp
    LIBS Qt6::Gui Qt6::Multimedia Qt6::Concurrent)
octave_add_test(tst_qmlmodules SOURCES util/qmlmodules.h util/qmlmodules.cpp LIBS Qt6::Qml)
octave_add_test(tst_units SOURCES util/units.h util/units.cpp)
octave_add_test(tst_clock SOURCES managers/clock.h managers/clock.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui)
octave_add_test(tst_fontcatalog SOURCES util/fontcatalog.h util/fontcatalog.cpp LIBS Qt6::Gui)
octave_add_test(tst_pathurl)
octave_add_test(tst_servicebridge
    SOURCES service/commandserver.h service/commandserver.cpp service/protocol.h
            service/serviceregistry.h service/serviceregistry.cpp service/servicebridge.h service/servicebridge.cpp
            service/metadescribe.h service/metadescribe.cpp
            service/websocket.h service/websocket.cpp service/staticfiles.h service/staticfiles.cpp
            util/variantlistmodel.h util/variantlistmodel.cpp
    LIBS Qt6::Gui Qt6::Qml Qt6::Network)
octave_add_test(tst_serviceoptions SOURCES service/serviceoptions.h service/serviceoptions.cpp LIBS Qt6::Network)

# docs/api/ matches what the built app publishes (--dump-api)
set(_apidocs_defines OCTAVE_APP_BINARY="$<TARGET_FILE:octave>")
if(OCTAVE_ENABLE_DOWNLOADS)
    list(APPEND _apidocs_defines OCTAVE_ENABLE_DOWNLOADS)
endif()
octave_add_test(tst_apidocs DEFINES ${_apidocs_defines})
add_dependencies(tst_apidocs octave)
# Everything the QML UI reaches is a published service member (step 7)
octave_add_test(tst_conformance)
octave_add_test(tst_websocket
    SOURCES service/commandserver.h service/commandserver.cpp service/websocket.h service/websocket.cpp
            service/staticfiles.h service/staticfiles.cpp
    LIBS Qt6::Gui Qt6::Qml Qt6::Network)
# --web-root: what a frontend directory serves, and what it refuses
octave_add_test(tst_staticfiles SOURCES service/staticfiles.h service/staticfiles.cpp)

# The Python SDK (sdk/python) against a real `octave --headless`
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME sdk_python
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s "${CMAKE_SOURCE_DIR}/sdk/python/tests")
    set_tests_properties(sdk_python PROPERTIES
        ENVIRONMENT "OCTAVE_BIN=$<TARGET_FILE:octave>;QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180)
endif()
