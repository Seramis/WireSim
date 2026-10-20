cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

project(WireSim)

enable_testing()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Recommended for cleaner C++23 code

# Release-only LTO: src/Simulator/*.cpp (Pin/Bus/Platform) is hot code called
# from header-only Device::Logic() implementations across many TUs. Without
# LTO the compiler can't inline those cross-TU calls; with it, the linker can,
# so Pin/Bus/Platform.cpp stay split by component instead of forcing their hot
# methods into headers. Debug builds skip it -- it slows links for no benefit
# when optimizations are off anyway.
include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
if(NOT LTO_SUPPORTED)
    message(STATUS "LTO not supported by this toolchain, skipping: ${LTO_ERROR}")
endif()

# Confirm (rather than assume) that Release actually gets -O3 -DNDEBUG: the
# hot loop in src/Simulator relies on it, and CMake's default Release flags
# are toolchain-provided, not guaranteed.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(FLAG "-O3" "-DNDEBUG")
        if(NOT CMAKE_CXX_FLAGS_RELEASE MATCHES "${FLAG}")
            string(APPEND CMAKE_CXX_FLAGS_RELEASE " ${FLAG}")
        endif()
    endforeach()
endif()
message(STATUS "Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")

# 1. Setup Verilator
find_package(verilator REQUIRED)

# Graphics devices (Mrdi1) need a window/render library.
find_package(SDL2 REQUIRED)

# 2. Extract Verilator Runtime Sources
# Some installations don't provide the 'verilator::runtime' target.
# We manually collect the required runtime files instead.
set(VERILATOR_RUNTIME_SOURCES
    "${VERILATOR_ROOT}/include/verilated.cpp"
    "${VERILATOR_ROOT}/include/verilated_threads.cpp" # Required if using threads
)

set(HW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/Devices")
set(DEVICE_LIBS "")

# --- Dynamic Hardware Discovery via *.f files ---
# Search for any file ending in .f within the Hardware directory
file(GLOB_RECURSE MANIFEST_FILES "${HW_ROOT}/*.f")

foreach(MANIFEST IN LISTS MANIFEST_FILES)
    # Get directory path and the filename without extension (e.g., "MOS6502")
    get_filename_component(MODULE_DIR "${MANIFEST}" DIRECTORY)
    get_filename_component(TOP_NAME "${MANIFEST}" NAME_WLE)

    # Set naming conventions
    set(MODULE_GEN_DIR "${MODULE_DIR}/_gen")
    set(PREFIX "V${TOP_NAME}") # Result: VMOS6502

    # Ensure the _gen directory exists
    file(MAKE_DIRECTORY "${MODULE_GEN_DIR}")

    # The manifest lists this device's Verilog source files, one per line
    # (blank lines and '#' comments ignored), relative to the manifest's directory.
    file(STRINGS "${MANIFEST}" MANIFEST_LINES)
    set(VERILOG_SOURCES "")
    foreach(LINE IN LISTS MANIFEST_LINES)
        string(STRIP "${LINE}" LINE)
        if(LINE AND NOT LINE MATCHES "^#")
            list(APPEND VERILOG_SOURCES "${MODULE_DIR}/${LINE}")
        endif()
    endforeach()

    message(STATUS "Found Manifest: ${MANIFEST}")
    message(STATUS "  Target Top Module: ${TOP_NAME}")
    message(STATUS "  Output Directory:  ${MODULE_GEN_DIR}")
    message(STATUS "  Sources: ${VERILOG_SOURCES}")

    # --- Create Static Library ---
    # Creates a library named (e.g.) MOS6502_lib. verilate() (provided by
    # find_package(verilator)) queries Verilator for the *actual* set of
    # generated files -- which Verilator 5.x splits across several
    # V<Top>___024root*.cpp / V<Top>__Syms*.cpp files depending on design
    # size -- and wires them all into the target. A hand-written
    # add_custom_command here would have to guess that file list and
    # silently drop files on anything but a trivial module.
    add_library(${TOP_NAME}_lib STATIC)
    verilate(${TOP_NAME}_lib
        TOP_MODULE ${TOP_NAME}
        PREFIX ${PREFIX}
        DIRECTORY ${MODULE_GEN_DIR}
        SOURCES ${VERILOG_SOURCES}
        INCLUDE_DIRS ${MODULE_DIR}
        VERILATOR_ARGS -Wno-fatal -Wno-lint -Wno-style
    )

    # Generated code, not ours to fix: silence C++ compiler warnings for it
    # regardless of whatever flags the including project/compiler adds later.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TOP_NAME}_lib PRIVATE -w)
    endif()

    list(APPEND DEVICE_LIBS ${TOP_NAME}_lib)

endforeach()

# 4. Shared OBJECT libraries
# WireSim, DeviceHarness and MachineHarness overlap heavily in which sources
# they need (the engine, the devices, Support). Previously each target ran
# its own file(GLOB_RECURSE ...) straight into its own add_executable(),
# so identical TUs (e.g. Platform.cpp) were compiled three times over, once
# per target's own object directory, with no sharing at all. Splitting the
# shared source sets into OBJECT libraries -- built once, linked into every
# executable that needs them -- removes that triplication. OBJECT (not
# STATIC) specifically: a static library only keeps archive members the
# linker finds a referenced symbol for, and HARNESS_REGISTER_DEVICE-style
# self-registration (src/Testing/HarnessCore.hpp) has no such reference --
# a STATIC library would silently drop registrar TUs. $<TARGET_OBJECTS:...>
# (what linking an OBJECT library expands to) always includes every object
# file, so registration keeps working.

file(GLOB_RECURSE SIMULATOR_SOURCES "src/Simulator/*.cpp")
add_library(SimulatorCore OBJECT ${SIMULATOR_SOURCES})
target_include_directories(SimulatorCore PRIVATE ${CMAKE_SOURCE_DIR})

file(GLOB_RECURSE SUPPORT_SOURCES "src/Support/*.cpp")
add_library(SupportCommon OBJECT ${SUPPORT_SOURCES})
target_include_directories(SupportCommon PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(SupportCommon PRIVATE SDL2::SDL2)

# The real (non-registration-shim) device implementations: shared by all
# three executables.
file(GLOB_RECURSE DEVICES_CORE_SOURCES "src/Devices/*.cpp")
list(FILTER DEVICES_CORE_SOURCES EXCLUDE REGEX "/_gen/")
list(FILTER DEVICES_CORE_SOURCES EXCLUDE REGEX "\\.(harness|machine-harness)\\.cpp$")
add_library(DevicesCore OBJECT ${DEVICES_CORE_SOURCES})
target_include_directories(DevicesCore PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(DevicesCore PRIVATE SDL2::SDL2)

# DeviceHarness/MachineHarness registration shims (<Name>.harness.cpp):
# shared by DeviceHarness and MachineHarness, but not WireSim, which has no
# need to depend on src/Testing/.
file(GLOB_RECURSE DEVICE_HARNESS_SHIM_SOURCES "src/Devices/*.harness.cpp")
add_library(DeviceHarnessShims OBJECT ${DEVICE_HARNESS_SHIM_SOURCES})
target_include_directories(DeviceHarnessShims PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(DeviceHarnessShims SYSTEM PRIVATE
    ${VERILATOR_ROOT}/include
    ${VERILATOR_ROOT}/include/vltstd
)
target_link_libraries(DeviceHarnessShims PRIVATE SDL2::SDL2)
if(DEVICE_LIBS)
    # Register8's harness shim reaches its Verilog model through
    # Register8.hpp's "_gen/VRegister8.h" -- that header in turn pulls in
    # Verilator's own <verilated.h> via the SYSTEM include above.
    target_link_libraries(DeviceHarnessShims PRIVATE ${DEVICE_LIBS})
endif()

# Machine-specific glue (e.g. Chipset), excluding each machine's own
# MachineHarness registration shim. Shared by WireSim and MachineHarness,
# not DeviceHarness (no machine involved there). Currently empty (the only
# file under src/Machines/*.cpp is a machine-harness shim) but kept for any
# future machine's non-shim .cpp; file(GLOB_RECURSE) matching nothing is not
# an error, but add_library(... OBJECT) with zero sources is, hence the guard.
file(GLOB_RECURSE MACHINES_CORE_SOURCES "src/Machines/*.cpp")
list(FILTER MACHINES_CORE_SOURCES EXCLUDE REGEX "\\.machine-harness\\.cpp$")
if(MACHINES_CORE_SOURCES)
    add_library(MachinesCore OBJECT ${MACHINES_CORE_SOURCES})
    target_include_directories(MachinesCore PRIVATE ${CMAKE_SOURCE_DIR})
    target_include_directories(MachinesCore SYSTEM PRIVATE
        ${VERILATOR_ROOT}/include
        ${VERILATOR_ROOT}/include/vltstd
    )
    target_link_libraries(MachinesCore PRIVATE SDL2::SDL2)
    if(DEVICE_LIBS)
        target_link_libraries(MachinesCore PRIVATE ${DEVICE_LIBS})
    endif()
endif()

# src/Testing/*.cpp minus each harness target's own entry point (HarnessMain.cpp,
# MachineHarnessMain.cpp), which are added directly to their own executable
# below. Shared by DeviceHarness and MachineHarness.
file(GLOB_RECURSE TESTING_CORE_SOURCES "src/Testing/*.cpp")
list(FILTER TESTING_CORE_SOURCES EXCLUDE REGEX "/(HarnessMain|MachineHarnessMain)\\.cpp$")
add_library(TestingCore OBJECT ${TESTING_CORE_SOURCES})
target_include_directories(TestingCore PRIVATE ${CMAKE_SOURCE_DIR})

# The vendored Verilator runtime, built once instead of once per executable.
add_library(VerilatorRuntime OBJECT ${VERILATOR_RUNTIME_SOURCES})
# SYSTEM: Verilator's own headers (verilated.h and friends) are reached from
# our files through this path whenever we #include a device's generated
# _gen/V<Device>.h. Marking it SYSTEM tells the compiler that code living
# inside those headers isn't ours to warn about, regardless of which of our
# TUs pulls them in or what warning flags that TU builds with.
target_include_directories(VerilatorRuntime SYSTEM PRIVATE
    ${VERILATOR_ROOT}/include
    ${VERILATOR_ROOT}/include/vltstd
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Vendored, not ours to fix: skip -Wall/-Wextra entirely rather than
    # applying and then overriding it, since this library compiles nothing else.
    target_compile_options(VerilatorRuntime PRIVATE -w)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(LIB IN ITEMS SimulatorCore SupportCommon DevicesCore DeviceHarnessShims TestingCore)
        target_compile_options(${LIB} PRIVATE -Wall -Wextra)
    endforeach()
    if(TARGET MachinesCore)
        target_compile_options(MachinesCore PRIVATE -Wall -Wextra)
    endif()
endif()

# Release-only LTO (see the comment near the top of this file) needs to reach
# every TU that calls into the Simulator/Devices hot path, not just whichever
# executable's own direct sources -- so it's applied to these shared libraries
# themselves, not (only) to the executables that link them.
if(LTO_SUPPORTED AND CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(LIB IN ITEMS SimulatorCore SupportCommon DevicesCore DeviceHarnessShims TestingCore VerilatorRuntime)
        set_property(TARGET ${LIB} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endforeach()
    if(TARGET MachinesCore)
        set_property(TARGET MachinesCore PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# Platform.hpp is the engine's umbrella header (pulls in Bus/Device/
# Diagnostics/LogicState/Pin plus several STL headers) and is included,
# directly or transitively through a device's own header, by nearly every
# TU below -- precompiling it once per library cuts repeated parsing of the
# same header graph across many device/test TUs. SupportCommon doesn't
# include it and is left out.
foreach(LIB IN ITEMS SimulatorCore DevicesCore DeviceHarnessShims TestingCore)
    target_precompile_headers(${LIB} PRIVATE "${CMAKE_SOURCE_DIR}/src/Simulator/Platform.hpp")
endforeach()
if(TARGET MachinesCore)
    target_precompile_headers(MachinesCore PRIVATE "${CMAKE_SOURCE_DIR}/src/Simulator/Platform.hpp")
endif()

# 5. The production binaries: one per machine, all built from src/Main.cpp.
# A machine describes itself in src/Machines/<Machine>/machine.cmake (discovered
# below, like the Verilog manifests above) by calling wiresim_add_machine() and
# one wiresim_add_firmware() per ROM variant -- nothing here or in the IDE
# configuration changes when a machine is added.
#
#   wiresim_add_machine(<Name> EXECUTABLE <target> HEADER <include path>
#                        DEFAULT_VARIANT <variant> DIR <machine dir> [TEST <.machinetest>])
#   (each firmware variant also gets a `run_<Name>_<variant>` target that builds and runs it)
#   wiresim_add_firmware(<Name> VARIANT <variant> [ARGS <build-firmware.sh args>...]
#                         [DEPENDS <file>...] [DEPENDS_DIRS <dir relative to machine>...])
#
# The executable is <build>/<target>; each firmware variant is assembled by the
# machine's scripts/build-firmware.sh into <build>/rom/<Name>-<variant> (what
# Main.cpp's --variant selects) and is a build dependency of the executable.
function(wiresim_add_machine NAME)
    cmake_parse_arguments(ARG "" "EXECUTABLE;HEADER;DEFAULT_VARIANT;DIR;TEST" "" ${ARGN})
    set(TARGET ${ARG_EXECUTABLE})

    add_executable(${TARGET} ${CMAKE_SOURCE_DIR}/src/Main.cpp)
    target_compile_definitions(${TARGET} PRIVATE
        MACHINE_HEADER="${ARG_HEADER}"
        MACHINE_CLASS=${NAME}
        MACHINE_NAME="${NAME}"
        MACHINE_DEFAULT_VARIANT="${ARG_DEFAULT_VARIANT}"
    )

    target_link_libraries(${TARGET} PRIVATE SimulatorCore SupportCommon DevicesCore VerilatorRuntime)
    if(TARGET MachinesCore)
        target_link_libraries(${TARGET} PRIVATE MachinesCore)
    endif()

    if(LTO_SUPPORTED AND CMAKE_BUILD_TYPE STREQUAL "Release")
        set_property(TARGET ${TARGET} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    # Fix Dependency Order: this forces CMake to build all Verilated libraries
    # before linking this target.
    if(DEVICE_LIBS)
        target_link_libraries(${TARGET} PRIVATE ${DEVICE_LIBS})
    endif()

    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_SOURCE_DIR}
    )
    target_include_directories(${TARGET} SYSTEM PRIVATE
        ${VERILATOR_ROOT}/include
        ${VERILATOR_ROOT}/include/vltstd
    )

    target_link_libraries(${TARGET} PRIVATE pthread m SDL2::SDL2)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TARGET} PRIVATE -Wall -Wextra)
    endif()

    set_property(GLOBAL APPEND PROPERTY WIRESIM_MACHINE_TARGETS ${TARGET})
    set_property(GLOBAL PROPERTY WIRESIM_${NAME}_EXECUTABLE ${TARGET})
    set_property(GLOBAL PROPERTY WIRESIM_${NAME}_DIR ${ARG_DIR})

    if(ARG_TEST)
        # Scripts name their ROM as rom/<Name>-<variant>, relative to the build directory.
        add_test(NAME machinetest.${NAME}
            COMMAND MachineHarness ${ARG_TEST}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    endif()
endfunction()

function(wiresim_add_firmware NAME)
    cmake_parse_arguments(ARG "" "VARIANT" "ARGS;DEPENDS;DEPENDS_DIRS" ${ARGN})
    get_property(EXECUTABLE GLOBAL PROPERTY WIRESIM_${NAME}_EXECUTABLE)
    get_property(MACHINE_DIR GLOBAL PROPERTY WIRESIM_${NAME}_DIR)

    set(SCRIPT ${MACHINE_DIR}/scripts/build-firmware.sh)
    set(WORK_DIR ${CMAKE_BINARY_DIR}/firmware/${NAME}-${ARG_VARIANT})
    set(ROM_DIR ${CMAKE_BINARY_DIR}/rom/${NAME}-${ARG_VARIANT})
    set(STAMP ${CMAKE_BINARY_DIR}/rom/${NAME}-${ARG_VARIANT}.stamp)

    set(INPUTS ${ARG_DEPENDS})
    foreach(DIR IN ITEMS Firmware scripts ${ARG_DEPENDS_DIRS})
        file(GLOB_RECURSE DIR_FILES CONFIGURE_DEPENDS "${MACHINE_DIR}/${DIR}/*")
        list(APPEND INPUTS ${DIR_FILES})
    endforeach()

    add_custom_command(
        OUTPUT ${STAMP}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${WORK_DIR} ${ROM_DIR}
        COMMAND ${SCRIPT} -o ${WORK_DIR} -r ${ROM_DIR} ${ARG_ARGS}
        COMMAND ${CMAKE_COMMAND} -E touch ${STAMP}
        DEPENDS ${INPUTS}
        COMMENT "Assembling ${NAME} firmware (${ARG_VARIANT})"
        VERBATIM
    )
    add_custom_target(${NAME}_firmware_${ARG_VARIANT} DEPENDS ${STAMP})
    add_dependencies(${EXECUTABLE} ${NAME}_firmware_${ARG_VARIANT})

    # `run_<Name>_<variant>`: builds, then runs the machine on this firmware in the terminal
    # (CMake Tools: "CMake: Build Target"). The script keeps stdin open when it isn't a terminal.
    add_custom_target(run_${NAME}_${ARG_VARIANT}
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-machine.sh $<TARGET_FILE:${EXECUTABLE}> --rom ${ROM_DIR}
        DEPENDS ${EXECUTABLE}
        USES_TERMINAL
        VERBATIM
    )
endfunction()

file(GLOB MACHINE_MANIFESTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Machines/*/machine.cmake")
foreach(MANIFEST IN LISTS MACHINE_MANIFESTS)
    include(${MANIFEST})
endforeach()

# 6. DeviceHarness: exercises one Sim::Device at a time, standalone, through
# a text script (see src/Testing/DeviceHarness.md) -- no CPU, ROM, or Chipset
# needed. Shares the engine and device libraries with WireSim, but not
# src/Machines (no machine involved) or Main.cpp (its own entry point).
add_executable(DeviceHarness src/Testing/HarnessMain.cpp)

target_link_libraries(DeviceHarness PRIVATE
    SimulatorCore SupportCommon DevicesCore DeviceHarnessShims TestingCore VerilatorRuntime
)

if(DEVICE_LIBS)
    target_link_libraries(DeviceHarness PRIVATE ${DEVICE_LIBS})
endif()

target_include_directories(DeviceHarness PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_include_directories(DeviceHarness SYSTEM PRIVATE
    ${VERILATOR_ROOT}/include
    ${VERILATOR_ROOT}/include/vltstd
)

target_link_libraries(DeviceHarness PRIVATE pthread m SDL2::SDL2)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(DeviceHarness PRIVATE -Wall -Wextra)
endif()

# 7. MachineHarness: boots one registered Machine from a pre-built ROM
# directory and runs it headless through a text script (see
# src/Testing/MachineHarness.md) -- the machine-level analogue of
# DeviceHarness above. Unlike DeviceHarness, this DOES need src/Machines
# (that's the whole point), but still not Main.cpp (its own entry point) or
# DeviceHarness's own HarnessMain.cpp.
file(GLOB_RECURSE MACHINE_HARNESS_SHIM_SOURCES "src/Machines/*.machine-harness.cpp")

add_executable(MachineHarness src/Testing/MachineHarnessMain.cpp ${MACHINE_HARNESS_SHIM_SOURCES})

target_link_libraries(MachineHarness PRIVATE
    SimulatorCore SupportCommon DevicesCore DeviceHarnessShims TestingCore VerilatorRuntime
)
if(TARGET MachinesCore)
    target_link_libraries(MachineHarness PRIVATE MachinesCore)
endif()

if(DEVICE_LIBS)
    target_link_libraries(MachineHarness PRIVATE ${DEVICE_LIBS})
endif()

target_include_directories(MachineHarness PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_include_directories(MachineHarness SYSTEM PRIVATE
    ${VERILATOR_ROOT}/include
    ${VERILATOR_ROOT}/include/vltstd
)

target_link_libraries(MachineHarness PRIVATE pthread m SDL2::SDL2)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MachineHarness PRIVATE -Wall -Wextra)
endif()
# 8. Tests: every device script under src/Devices runs through DeviceHarness;
# machine scripts are registered by their machine.cmake. `ctest --test-dir
# build/<preset>` runs them against what is already built; the `check` target
# builds everything they need first, then runs ctest.
file(GLOB_RECURSE DEVICE_TEST_SCRIPTS CONFIGURE_DEPENDS "src/Devices/*.devtest")
foreach(SCRIPT IN LISTS DEVICE_TEST_SCRIPTS)
    get_filename_component(SCRIPT_NAME "${SCRIPT}" NAME_WE)
    add_test(NAME devtest.${SCRIPT_NAME} COMMAND DeviceHarness ${SCRIPT})
endforeach()

get_property(MACHINE_TARGETS GLOBAL PROPERTY WIRESIM_MACHINE_TARGETS)
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS DeviceHarness MachineHarness ${MACHINE_TARGETS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
)
