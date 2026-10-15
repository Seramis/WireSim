# BenEater6502: Microsoft BASIC + WozMon from the vendored msbasic submodule
# (default) and the .machinetest wiring-regression image. See the
# wiresim_add_machine()/wiresim_add_firmware() documentation in the top-level
# CMakeLists.txt.
wiresim_add_machine(BenEater6502
    EXECUTABLE BenEater6502
    HEADER Machines/BenEater6502/BenEater6502.hpp
    DEFAULT_VARIANT basic
    DIR ${CMAKE_CURRENT_LIST_DIR}
    TEST ${CMAKE_CURRENT_LIST_DIR}/BenEater6502.machinetest)

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/vendor/msbasic/msbasic.s)
    wiresim_add_firmware(BenEater6502 VARIANT basic DEPENDS_DIRS vendor/msbasic)
    wiresim_add_firmware(BenEater6502 VARIANT machinetest ARGS -t DEPENDS_DIRS vendor/msbasic)
else()
    message(WARNING "BenEater6502: vendor/msbasic is empty, so no firmware will be built; run: git submodule update --init")
endif()
