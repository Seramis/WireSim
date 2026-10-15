# My6502: the real kernel + WozMon (default), the debug.asm scratch program with
# cycle-tagged tracing, and the .machinetest wiring-regression image. See the wiresim_add_machine()/
# wiresim_add_firmware() documentation in the top-level CMakeLists.txt.
wiresim_add_machine(My6502
    EXECUTABLE WireSim
    HEADER Machines/My6502/My6502.hpp
    DEFAULT_VARIANT kernel
    DIR ${CMAKE_CURRENT_LIST_DIR}
    TEST ${CMAKE_CURRENT_LIST_DIR}/My6502.machinetest)

wiresim_add_firmware(My6502 VARIANT debug)
wiresim_add_firmware(My6502 VARIANT kernel ARGS -k)
wiresim_add_firmware(My6502 VARIANT machinetest
    ARGS ${CMAKE_CURRENT_LIST_DIR}/My6502.machinetest.asm
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/My6502.machinetest.asm)
