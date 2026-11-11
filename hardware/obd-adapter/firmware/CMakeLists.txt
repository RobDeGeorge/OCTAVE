# OCTAVE OBD adapter firmware: bring-up skeleton for the STM32G474RET6.
#
#   cmake --preset arm  && cmake --build --preset arm     obd-adapter-rev0.elf, obd-adapter-nucleo.elf
#   cmake --preset host && cmake --build --preset host && ctest --preset host
#
# Stand-alone on purpose: the repository's top-level CMakeLists.txt and CI do not build this.
cmake_minimum_required(VERSION 3.21)
project(octave_obd_adapter_firmware VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # gnu11: __attribute__, asm in the CMSIS headers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FW_WERROR "Treat compiler warnings as errors" ON)

set(FW_WARNINGS -Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes)
if(FW_WERROR)
    list(APPEND FW_WARNINGS -Werror)
endif()

# the logic that needs no hardware: built into the firmware and into the host tests
set(FW_PURE_SOURCES
    src/can_timing.c
    src/console.c
    src/fmt.c
    src/safe_pins.c
    src/vbat.c
)

if(NOT CMAKE_CROSSCOMPILING)
    # ------------------------------------------------------------------ host tests
    option(FW_SANITIZE "Build the host tests with AddressSanitizer and UBSan" ON)
    enable_testing()

    add_library(fw_pure STATIC ${FW_PURE_SOURCES})
    target_include_directories(fw_pure PUBLIC src)
    target_compile_options(fw_pure PUBLIC ${FW_WARNINGS})
    if(FW_SANITIZE)
        target_compile_options(fw_pure PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
        target_link_options(fw_pure PUBLIC -fsanitize=address,undefined)
    endif()

    foreach(name vbat fmt can_timing console pins)
        add_executable(test_${name} tests/test_${name}.c)
        target_link_libraries(test_${name} PRIVATE fw_pure)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()

    # board_pins.h against the board generator and the pin check record. Exit code 77 (the generator
    # could not be imported: no KiCad python on this machine) shows as "skipped", not as a pass.
    find_program(FW_PYTHON3 NAMES python3 PATHS /usr/bin NO_DEFAULT_PATH)   # the system one, not a venv
    if(NOT FW_PYTHON3)
        find_program(FW_PYTHON3 NAMES python3)
    endif()
    if(FW_PYTHON3)
        add_test(NAME pins_header_current
                 COMMAND ${FW_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_pins.py --check)
        set_tests_properties(pins_header_current PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    return()
endif()

# ---------------------------------------------------------------------- firmware
enable_language(ASM)

# ST's device header and startup file and Arm's core header: nine files, fetched at pinned commits
# with a SHA-256 each. Nothing of them is committed here. Offline: point FW_DEPS_DIR at a directory
# that already holds them (same layout); files whose hash matches are not downloaded again.
set(FW_DEPS_DIR "${CMAKE_BINARY_DIR}/deps" CACHE PATH "Where the pinned CMSIS files are kept")

set(FW_DEVICE_URL "https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-g4/25664ddc3a7624ae9627ae8c4c672073dc5b2539")  # tag v1.2.6
set(FW_CORE_URL   "https://raw.githubusercontent.com/STMicroelectronics/cmsis-core/1a2f783395dd72778a98ebe7771f36ca1c30a35e")       # tag v5.9.0

function(fw_fetch url dest sha256)
    set(path "${FW_DEPS_DIR}/${dest}")
    if(EXISTS "${path}")
        file(SHA256 "${path}" have)
        if(have STREQUAL sha256)
            return()
        endif()
        message(STATUS "firmware deps: ${dest} does not match its pinned hash, fetching it again")
    endif()
    message(STATUS "firmware deps: fetching ${dest}")
    file(DOWNLOAD "${url}" "${path}" EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        file(REMOVE "${path}")
        message(FATAL_ERROR "firmware deps: could not fetch ${url}: ${status}\n"
                            "Offline? Put the file at ${path} (see README.md, Dependencies).")
    endif()
endfunction()

fw_fetch("${FW_DEVICE_URL}/Include/stm32g4xx.h"        cmsis-device-g4/Include/stm32g4xx.h
         493603c55af538bc431713b4f9880b5e242447cea232ca7459d3ca59c71dd48d)
fw_fetch("${FW_DEVICE_URL}/Include/stm32g474xx.h"      cmsis-device-g4/Include/stm32g474xx.h
         e7e5851721452c415c627e233d0a617b72ba891718a4f12a495b20dee25d82b0)
fw_fetch("${FW_DEVICE_URL}/Include/system_stm32g4xx.h" cmsis-device-g4/Include/system_stm32g4xx.h
         36304c07e9cee84064015ebd7ae517af6f466c466c1bbf5e342c57a296601db2)
fw_fetch("${FW_DEVICE_URL}/Source/Templates/gcc/startup_stm32g474xx.s"
         cmsis-device-g4/Source/Templates/gcc/startup_stm32g474xx.s
         2d1952a86701643338fbe284341c9099f96a52608d40cbda35212bf14375ad2a)
fw_fetch("${FW_DEVICE_URL}/LICENSE.md"                 cmsis-device-g4/LICENSE.md
         44fd163ed2e04fe99f519f1093c2c39b21d9cc9371071de902ff0c2c8c3c57dc)
fw_fetch("${FW_CORE_URL}/Include/core_cm4.h"           cmsis-core/Include/core_cm4.h
         f5b63d52dd1557b15ca414cb59c264f595a7b5669db2a5878dffe22f4caedc8c)
fw_fetch("${FW_CORE_URL}/Include/cmsis_version.h"      cmsis-core/Include/cmsis_version.h
         184c19fd3ee73632edf35a0b4d49cd48be75fbf49e6ccb19d9db05fa83bea4b3)
fw_fetch("${FW_CORE_URL}/Include/cmsis_compiler.h"     cmsis-core/Include/cmsis_compiler.h
         b51963d271c1571ca3463654c76aa7ea50c4eac5d0a2710df10414ac64d9e0f8)
fw_fetch("${FW_CORE_URL}/Include/cmsis_gcc.h"          cmsis-core/Include/cmsis_gcc.h
         43bfd1fe69fbbc2ca70aaf7d43cc3e20f16e0c7f66e1bdef9b51af6dc14b2617)
fw_fetch("${FW_CORE_URL}/Include/mpu_armv7.h"          cmsis-core/Include/mpu_armv7.h
         29206b52ee02290ed6f5a5415ebd4187de802cf176d9b3cb844390d8e5571372)

# No FPU use: the skeleton has no floating point, and a soft-float build cannot fault on a lazy FPU
# context before anyone enables the coprocessor.
set(FW_ARCH -mcpu=cortex-m4 -mthumb -mfloat-abi=soft)
set(FW_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/src/stm32g474re.ld")

set(FW_SOURCES
    ${FW_PURE_SOURCES}
    src/adc.c
    src/board.c
    src/can.c
    src/clock.c
    src/cmds.c
    src/i2c.c
    src/kline.c
    src/main.c
    src/system.c
    src/uart.c
)

function(fw_add_board name define)
    set(target obd-adapter-${name})
    add_executable(${target} ${FW_SOURCES}
                   "${FW_DEPS_DIR}/cmsis-device-g4/Source/Templates/gcc/startup_stm32g474xx.s")
    set_target_properties(${target} PROPERTIES SUFFIX .elf LINK_DEPENDS "${FW_LINKER_SCRIPT}")
    target_compile_definitions(${target} PRIVATE STM32G474xx ${define}
                               FW_VERSION="${PROJECT_VERSION}" FW_BOARD_NAME="${name}")
    target_include_directories(${target} PRIVATE src)
    # -isystem: the vendor headers are not ours to make warning-clean
    target_include_directories(${target} SYSTEM PRIVATE
                               "${FW_DEPS_DIR}/cmsis-device-g4/Include" "${FW_DEPS_DIR}/cmsis-core/Include")
    target_compile_options(${target} PRIVATE ${FW_ARCH} -ffunction-sections -fdata-sections
                           -fno-common
                           $<$<COMPILE_LANGUAGE:C>:${FW_WARNINGS}>)
    target_link_options(${target} PRIVATE ${FW_ARCH} -nostartfiles --specs=nano.specs
                        -T "${FW_LINKER_SCRIPT}" -Wl,--gc-sections -Wl,--fatal-warnings
                        -Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${target}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        VERBATIM)
endfunction()

fw_add_board(rev0   BOARD_REV0)
fw_add_board(nucleo BOARD_NUCLEO)
