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

octave_add_test(tst_elm327protocol SOURCES managers/elm327protocol.cpp)
octave_add_test(tst_adaptivebitrate)
octave_add_test(tst_settingsmanager SOURCES managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui)
octave_add_test(tst_dashboardmanager SOURCES managers/dashboardmanager.h managers/dashboardmanager.cpp LIBS Qt6::Gui)
octave_add_test(tst_esp32readbuffer SOURCES managers/esp32volumemanager.h managers/esp32volumemanager.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui Qt6::SerialPort)

# Integration: the real OBD stack against the simulated adapter (src/dev/)
if(OCTAVE_DEV_TOOLS)
    octave_add_test(tst_obdsim
        SOURCES managers/obdmanager.h managers/obdmanager.cpp
                managers/elm327protocol.cpp managers/rfcommsocket.h managers/rfcommsocket.cpp
                managers/settingsmanager.h managers/settingsmanager.cpp
                dev/carsim.h dev/carsim.cpp dev/simelm327.h dev/simelm327.cpp
        LIBS Qt6::Gui Qt6::SerialPort)
endif()
