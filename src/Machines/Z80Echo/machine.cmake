# Z80Echo: a Z80 + 16C550 UART computer that echoes its serial input (default),
# and the .machinetest wiring-regression image. Firmware is assembled with
# sjasmplus. See the wiresim_add_machine()/wiresim_add_firmware() documentation
# in the top-level CMakeLists.txt.
wiresim_add_machine(Z80Echo
    EXECUTABLE Z80Echo
    HEADER Machines/Z80Echo/Z80Echo.hpp
    DEFAULT_VARIANT echo
    DIR ${CMAKE_CURRENT_LIST_DIR}
    TEST ${CMAKE_CURRENT_LIST_DIR}/Z80Echo.machinetest)

find_program(SJASMPLUS sjasmplus)
if(SJASMPLUS)
    wiresim_add_firmware(Z80Echo VARIANT echo)
    wiresim_add_firmware(Z80Echo VARIANT machinetest ARGS -t)
else()
    message(WARNING "Z80Echo: sjasmplus not found, so no firmware will be built; see src/Machines/Z80Echo/AGENTS.md")
endif()
