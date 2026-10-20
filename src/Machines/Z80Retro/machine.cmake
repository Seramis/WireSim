# Z80Retro: the Z80-Retro single board computer (Z80, flash, banked SRAM, CTC,
# SIO, SD card) booting CP/M 2.2 from an SD card image. The boot flash and BIOS
# are the vendored 2063-Z80-cpm sources, assembled with z80asm; the card image is
# made with cpmtools. See the wiresim_add_machine()/wiresim_add_firmware()
# documentation in the top-level CMakeLists.txt.
wiresim_add_machine(Z80Retro
    EXECUTABLE Z80Retro
    HEADER Machines/Z80Retro/Z80Retro.hpp
    DEFAULT_VARIANT cpm
    DIR ${CMAKE_CURRENT_LIST_DIR}
    TEST ${CMAKE_CURRENT_LIST_DIR}/Z80Retro.machinetest)

find_program(Z80ASM z80asm)
find_program(MKFS_CPM mkfs.cpm)
set(CPM_SOURCES ${CMAKE_CURRENT_LIST_DIR}/vendor/2063-Z80-cpm/cpm-2.2/src/cpm22.asm)
if(Z80ASM AND MKFS_CPM AND EXISTS ${CPM_SOURCES})
    wiresim_add_firmware(Z80Retro VARIANT cpm
        DEPENDS_DIRS
            vendor/2063-Z80-cpm/boot vendor/2063-Z80-cpm/retro vendor/2063-Z80-cpm/lib vendor/2063-Z80-cpm/libretro
            vendor/2063-Z80-cpm/cpm-2.2/src vendor/2063-Z80-cpm/cpm-2.2/filesystem)
else()
    message(WARNING "Z80Retro: z80asm, cpmtools or the vendor/2063-Z80-cpm submodule (git submodule update --init --recursive) is missing, so no firmware will be built; see src/Machines/Z80Retro/AGENTS.md")
endif()
