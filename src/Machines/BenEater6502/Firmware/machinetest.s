; Wiring regression image for BenEater6502.machinetest -- exercises every
; decoded window through the real CPU/bus, then parks. Not part of the
; machine's normal firmware (that is the vendored msbasic image).
.setcpu "65C02"

ACIA_STATUS = $5001
ACIA_CMD    = $5002
ACIA_CTRL   = $5003
VIA_PORTB   = $6000
VIA_PORTA   = $6001
VIA_DDRB    = $6002
VIA_DDRA    = $6003
VIA_T1CL    = $6004
VIA_T1CH    = $6005
VIA_T1LL    = $6006
VIA_IER     = $600E

.segment "HEADER"
rom_marker:     .byte $5A, $A5

.segment "CODE"
reset:
		sei
		cld
		ldx #$FF
		txs

		; SRAM: low and the top of the 16 KiB SRAM ($3FFF). $4000-$7FFF holds no
		; RAM, only the ACIA and VIA windows; a read there must not return what
		; was just written.
		lda #$A5
		sta $0300
		lda $0300
		sta $0200
		lda #$44
		sta $3FFF
		lda #$11
		sta $5010
		lda #$22
		sta $6010
		lda $3FFF
		sta $0201
		lda $5010
		sta $0202
		lda $6010
		sta $0203

		; ACIA: control/command round trip, status, and register-select
		; aliasing across its 16-byte window ($500B -> RS=3 -> control).
		lda #$1F
		sta ACIA_CTRL
		lda #$03                ; DTR on, receiver IRQ off
		sta ACIA_CMD
		lda ACIA_CTRL
		sta $0210
		lda ACIA_CMD
		sta $0211
		lda $500B
		sta $0212
		lda ACIA_STATUS
		sta $0213

		; VIA: port/direction registers and the T1 latch round trip.
		lda #$FF
		sta VIA_DDRB
		lda #$01
		sta VIA_DDRA
		lda #$0A                ; E low, so the LCD sees no strobe
		sta VIA_PORTB
		lda VIA_PORTB
		sta $0220
		lda VIA_DDRB
		sta $0221
		lda VIA_DDRA
		sta $0222
		lda #$34
		sta VIA_T1LL
		lda VIA_T1LL
		sta $0223
		lda #$00
		sta VIA_PORTA           ; PA0 low = host may send
		lda VIA_PORTA
		sta $0224

		; EEPROM: reads through $8000-$FFFF, writes ignored.
		lda rom_marker
		sta $0230
		lda #$FF
		sta rom_marker
		lda rom_marker
		sta $0231
		lda $FFFC
		sta $0232
		lda $FFFD
		sta $0233

		; Interrupt wiring: a VIA Timer 1 time-out must reach the CPU's /IRQ.
		lda #$00
		sta $0240
		lda #$00
		sta VIA_T1CL
		lda #$02
		sta VIA_T1CH            ; load and start T1 (~512 cycles)
		lda #$C0                ; enable the T1 interrupt
		sta VIA_IER
		cli
wait_irq:
		lda $0240
		beq wait_irq
		sei

		lda #$77
		sta $0241               ; reached the end
park:
		jmp park

irq:
		pha
		lda VIA_T1CL            ; reading T1CL clears the T1 flag
		lda #$40
		sta VIA_IER             ; disable the T1 interrupt again
		inc $0240
		pla
		rti

.segment "RESETVEC"
		.word reset             ; NMI
		.word reset             ; RESET
		.word irq               ; IRQ/BRK
