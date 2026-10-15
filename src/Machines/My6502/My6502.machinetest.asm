; Scratch firmware for My6502.machinetest -- a comprehensive MachineHarness
; regression covering Chipset's address decode across three components:
; Register8 ($C008), a RAM range ($0300-$030F) accessed by the CPU through
; indexed addressing, and the ST16C550 UART's loopback FIFO path
; ($C000-$C007). Proves the whole bus round trip for each, not just that the
; CPU/peripheral each work in isolation (that's DeviceHarness's job, already
; done before any of them was eligible for this machine's component list).
; See My6502.machinetest for the full list of asserted final state, and
; src/Testing/MachineHarness.md for the script grammar this feeds.
.org $C100

HW_UART_RHR_THR = $C000
HW_UART_IER     = $C001
HW_UART_MCR     = $C004
HW_UART_SPR     = $C007

START:
	; ---- Register8 round trip ($C008) -- Chipset's simplest peripheral decode ----
	LDA #$42
	STA $C008       ; Register8 write -- exercises Reg8CEB/Reg8WEB
	LDA #$00        ; clobber A so the next load can't trivially "pass" by accident
	LDA $C008       ; Register8 read-back -- exercises Reg8OEB
	STA $0200       ; MachineHarness checks ram[0x200]

	; ---- RAM stress: indexed writes/reads across a 16-byte range ----
	LDX #$00
FILL_LOOP:
	TXA
	EOR #$A5
	STA $0300,X
	INX
	CPX #$10
	BNE FILL_LOOP

	LDA #$00
	STA $0311       ; mismatch counter -- stays 0 if every byte reads back correctly
	LDX #$00
VERIFY_LOOP:
	TXA
	EOR #$A5
	STA $0310       ; expected value, scratch
	LDA $0300,X
	CMP $0310
	BEQ VERIFY_OK
	INC $0311
VERIFY_OK:
	INX
	CPX #$10
	BNE VERIFY_LOOP

	; ---- ST16C550 UART: loopback FIFO fill/drain + register state ----
	LDA #$10            ; MCR.LOOPBACK
	STA HW_UART_MCR
	LDA #$01            ; IER.RECEIVE_READY
	STA HW_UART_IER
	LDA #$5A
	STA HW_UART_SPR     ; scratch-pad round trip

	LDX #$00
UART_FILL:
	TXA
	CLC
	ADC #$10            ; bytes $10..$14
	STA HW_UART_RHR_THR ; THR write -- loopback feeds _rxFifo directly, no transport needed
	INX
	CPX #$05
	BNE UART_FILL

	LDA HW_UART_RHR_THR ; RHR read -- drains the oldest byte ($10)
	STA $0320
	LDA HW_UART_RHR_THR ; drains the next byte ($11)
	STA $0321

DONE:
	JMP DONE        ; park so execution can't fall into NMI/RTI

NMI:
	RTI

IRQ:
	RTI

.segment "VECTORS"
	.word NMI     ; NMI
	.word START   ; RESET
	.word IRQ     ; IRQ
