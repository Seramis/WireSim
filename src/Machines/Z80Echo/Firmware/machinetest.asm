; Wiring-regression image for Z80Echo.machinetest (see that file for what each
; section proves). Pushes values through every decoded window with the real
; CPU, stores what it read in SRAM at RES, then parks in HALT at `done`.

	include "hw.inc"
	include "rom.inc"

RES             equ RAM_BASE + 0x100
RES_ROM_FIRST   equ RES + 4
RES_ROM_LAST    equ RES + 5
RES_ROM_WRITE   equ RES + 6
RES_SPR         equ RES + 7
RES_LCR         equ RES + 8
RES_MCR         equ RES + 9
RES_MEM_NEAR    equ RES + 10    ; memory read at $8087, next to the UART's ports
RES_PORT_GAP    equ RES + 11    ; port read from outside the UART window
RES_IRQ_COUNT   equ RES + 0x10
RES_IRQ_BYTE    equ RES + 0x11
RES_DONE        equ RES + 0x20

ROM_LAST_BYTE   equ 0x7FFF      ; last cell of the EEPROM
ROM_PROBE       equ 0x7000      ; a cell holding ROM_PROBE_VALUE, written to below
ROM_PROBE_VALUE equ 0x3C

	org 0x0000
	di
	ld sp, 0x0000               ; first push lands at $FFFF, the top of SRAM
	jp main

	; IM 1 handler: take the received byte (which clears the UART's INT) and count the interrupt.
	org 0x0038
irq:
	push af
	in a, (UART_RBR)
	ld (RES_IRQ_BYTE), a
	ld a, (RES_IRQ_COUNT)
	inc a
	ld (RES_IRQ_COUNT), a
	pop af
	ei
	reti

	org 0x0100
main:
	; SRAM: one byte under each of A14/A13 set and clear, A15 always set (the top
	; cells belong to the stack, so the last probe sits below them). The
	; test script checks the cells directly (SRAM index = address - $8000) and
	; the read-backs below.
	ld a, 0x11
	ld (0x8000), a
	ld a, 0x22
	ld (0xC000), a
	ld a, 0x33
	ld (0xFFF0), a
	ld a, 0x44
	ld (0xA000), a
	ld a, (0x8000)
	ld (RES + 0), a
	ld a, (0xC000)
	ld (RES + 1), a
	ld a, (0xFFF0)
	ld (RES + 2), a
	ld a, (0xA000)
	ld (RES + 3), a

	; EEPROM: first and last cell read through the decode, and a write that
	; must neither change the cell nor land in SRAM (index $7000 = address $F000).
	ld a, (0x0000)
	ld (RES_ROM_FIRST), a
	ld a, (ROM_LAST_BYTE)
	ld (RES_ROM_LAST), a
	xor a
	ld (ROM_PROBE), a
	ld a, (ROM_PROBE)
	ld (RES_ROM_WRITE), a

	; UART registers through I/O ports. SPR, LCR and MCR sit at A0-A2 = 7, 3, 4,
	; so a swapped or stuck address line shows as a wrong read-back.
	ld a, 0xC3
	out (UART_SPR), a
	in a, (UART_SPR)
	ld (RES_SPR), a
	ld a, 0x1B
	out (UART_LCR), a
	in a, (UART_LCR)
	ld (RES_LCR), a
	ld a, 0x03
	out (UART_MCR), a
	in a, (UART_MCR)
	ld (RES_MCR), a

	; Ports outside $80-$87 (A3-A7 differ) must not reach the UART: SPR keeps
	; $C3, and reading such a port leaves the data bus undriven (reads 0).
	ld a, 0x3C
	out (0x97), a
	out (0x8F), a
	out (0x07), a
	in a, (0x97)
	ld (RES_PORT_GAP), a
	; A memory access at an address that looks like a UART port is SRAM only.
	ld a, 0x3C
	ld (0x8087), a
	ld a, (0x8087)
	ld (RES_MEM_NEAR), a

	; UART /INT -> Z80 /INT (mode 1). With MCR loop-back a byte written to THR
	; lands in the receive FIFO and raises INT; the handler must run exactly once.
	ld a, LCR_8N1
	out (UART_LCR), a
	xor a
	ld (RES_IRQ_COUNT), a
	ld a, MCR_LOOP | MCR_DTR | MCR_RTS | MCR_OP2
	out (UART_MCR), a
	ld a, FCR_ENABLE | FCR_RX_CLR
	out (UART_FCR), a
	ld a, IER_RX
	out (UART_IER), a
	im 1
	ei
	ld a, 0x5A
	out (UART_THR), a
	ld b, 0
irq_wait:
	djnz irq_wait               ; 256 iterations: far longer than the interrupt takes
	di
	xor a
	out (UART_IER), a

	ld a, 0x77
	ld (RES_DONE), a
	jp done

	org 0x0700
done:
	halt

	org ROM_PROBE
	db ROM_PROBE_VALUE
	org ROM_LAST_BYTE
	db 0x5A

	savebin ROM_BIN, 0x0000, 0x8000
