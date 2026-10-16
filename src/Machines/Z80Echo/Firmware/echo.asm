; Echoes every byte received on the 16C550 UART back to its sender.
; Polled, 8N1, interrupts never enabled. See ../AGENTS.md.

	include "hw.inc"
	include "rom.inc"

	org 0x0000
reset:
	di
	ld sp, 0x0000               ; first push lands at $FFFF, the top of SRAM
	jp main

	org 0x0066
nmi:
	retn                        ; NMI is not wired; return if it ever fires

	org 0x0100
main:
	; 16C550 setup: divisor latch (the model has no baud timing), 8N1, FIFOs on,
	; DTR/RTS/OP2 asserted so the host end of the line may send.
	ld a, LCR_DLAB
	out (UART_LCR), a
	ld a, 1
	out (UART_DLL), a
	xor a
	out (UART_DLM), a
	ld a, LCR_8N1
	out (UART_LCR), a
	ld a, FCR_ENABLE | FCR_RX_CLR
	out (UART_FCR), a
	ld a, MCR_DTR | MCR_RTS | MCR_OP2
	out (UART_MCR), a

echo:
	in a, (UART_LSR)
	bit LSR_DR, a
	jr z, echo                  ; wait for a received byte
	in a, (UART_RBR)
	ld c, a                     ; keep it while polling the transmitter
tx_wait:
	in a, (UART_LSR)
	bit LSR_THRE, a
	jr z, tx_wait
	ld a, c
	out (UART_THR), a
	jr echo

	savebin ROM_BIN, 0x0000, 0x8000
