# Toolchain file for the two firmware targets: GNU Arm Embedded (arm-none-eabi-gcc) from PATH,
# or from -DARM_TOOLCHAIN_BIN=/path/to/bin.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(ARM_TOOLCHAIN_BIN "" CACHE PATH "Directory holding arm-none-eabi-gcc (empty: search PATH)")
if(ARM_TOOLCHAIN_BIN)
    set(_arm_prefix "${ARM_TOOLCHAIN_BIN}/arm-none-eabi-")
else()
    set(_arm_prefix "arm-none-eabi-")
endif()

set(CMAKE_C_COMPILER   "${_arm_prefix}gcc")
set(CMAKE_ASM_COMPILER "${_arm_prefix}gcc")
set(CMAKE_OBJCOPY      "${_arm_prefix}objcopy" CACHE FILEPATH "objcopy")
set(CMAKE_SIZE         "${_arm_prefix}size"    CACHE FILEPATH "size")

# a bare-metal executable cannot be linked without our linker script, so compiler checks build a library
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARM_TOOLCHAIN_BIN)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
