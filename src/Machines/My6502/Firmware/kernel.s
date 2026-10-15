; KERNEL API
.scope KERNEL
    GETC = kernel_getc
    PUTC = kernel_putc
.endscope

UART_BUFFER_LOC = $0300

; Flow-control watermarks against the 256-byte software ring buffer below (not the
; 16-byte hardware FIFO, which the IRQ handler drains completely on every entry --
; by the time we'd check it, it's already empty). Hysteresis between the two keeps
; MCR.RTS from flapping right at a single threshold.
UART_RTS_HIGH_WATER = 192  ; buffer this full: deassert RTS, ask the sender to pause
UART_RTS_LOW_WATER  = 64   ; buffer drained below this: reassert RTS, resume

.zeropage
UART_BUF_WRITE: .res 1    ; Write pointer (where IRQ puts data)
UART_BUF_READ: .res 1    ; Read pointer (where PUTC takes data)

; KERNEL Implementation
.segment "KERNEL"

; Ret: A, carry
.proc kernel_getc
    jsr uart_buffer_size
    beq @check_hw
    phx
    jsr uart_buffer_read
    plx
    pha                  ; uart_rts_update clobbers A; the byte we just read must survive
    jsr uart_rts_update  ; buffer just shrank -- may cross the low water mark
    pla
    jmp @found
@check_hw:
    lda HW::UART + UART::REG::LSR_
    and #UART::LSR_::DATA_READY
    beq @empty          ; Hardware is also empty

    ; Data is in UART but IRQ hasn't fired/processed it yet
    lda HW::UART + UART::REG::RHR
    jmp @found
@empty:
    clc
    rts
@found:
    sec
    rts
.endproc


.proc kernel_putc
    pha
@wait_tx:
    ; Check if we have room for sending
    lda HW::UART + UART::REG::LSR_
    and #UART::LSR_::TH_EMPTY
    beq @wait_tx

    ; We have room! Send.
    pla
    sta HW::UART + UART::REG::THR
    rts
.endproc

; Circular buffer
; Ret: A
.proc uart_buffer_size
    lda UART_BUF_WRITE
    sec
    sbc UART_BUF_READ
    rts
.endproc

; In: A
; Mod: X
.proc uart_buffer_write
    ; Input: A = Byte to write. If the buffer is full, the byte is dropped --
    ; this should only happen under RTS/CTS latency (see uart_rts_update), not
    ; in steady state.
    pha
    jsr uart_buffer_size
    cmp #255
    beq @full
    pla
    ldx UART_BUF_WRITE
    sta UART_BUFFER_LOC, x
    inc UART_BUF_WRITE
    rts
@full:
    pla
    rts
.endproc

; Mod: A
; Deassert MCR.RTS once the software buffer crosses UART_RTS_HIGH_WATER (ask the
; sender to pause), reassert once it drops below UART_RTS_LOW_WATER (resume). The
; hysteresis band between the two watermarks is left alone to avoid flapping.
; Current RTS state is read back from MCR itself (the 16C550 echoes the last
; write) rather than shadowed in zero page, so steady-state calls (almost every
; getc/IRQ byte, since the buffer spends most of its time under the low watermark)
; skip the MCR write -- and the UpdateFlowControl/RTS-listener fan-out it triggers --
; when nothing actually changed.
.proc uart_rts_update
    jsr uart_buffer_size
    cmp #UART_RTS_HIGH_WATER
    bcs @pause
    cmp #UART_RTS_LOW_WATER
    bcc @resume
    rts
@pause:
    lda HW::UART + UART::REG::MCR
    and #UART::MCR::RTS_
    beq @done           ; already paused -- nothing to write
    lda #(UART::MCR::DTR | UART::MCR::OP2)
    sta HW::UART + UART::REG::MCR
@done:
    rts
@resume:
    lda HW::UART + UART::REG::MCR
    and #UART::MCR::RTS_
    bne @done            ; already resumed -- nothing to write
    lda #(UART::MCR::DTR | UART::MCR::RTS_ | UART::MCR::OP2)
    sta HW::UART + UART::REG::MCR
    rts
.endproc

; Ret: A, carry
; Mod: X, A
.proc uart_buffer_read
    ldx UART_BUF_READ
    lda UART_BUFFER_LOC, x
    inc UART_BUF_READ
    rts
.endproc

; Mod: A
.proc uart_init
    ; Initialize pointers
    stz UART_BUF_WRITE
    stz UART_BUF_READ

    ; 1. Enable Access to Divisor Latches (DLAB = 1)
    ; This allows us to set the baud rate.
    lda #UART::LCR::DLAB
    sta HW::UART + UART::REG::LCR

    ; 2. Set Baud Rate (9600 Baud @ 1.8432 MHz)
    ; Divisor = 12 ($000C)
    lda #$0C    ; LSB of divisor
    sta HW::UART + UART::REG::DLL
    lda #$00    ; MSB of divisor
    sta HW::UART + UART::REG::DLM

    ; 3. Set Data Format & Disable DLAB
    ; Format: 8 Data Bits, 1 Stop Bit, No Parity (8N1)
    ; Bit 7 (DLAB) is set to 0 to return to normal register access.
    lda #(UART::LCR::WORD_LEN_0 | UART::LCR::WORD_LEN_1)
    sta HW::UART + UART::REG::LCR

    ; 4. Enable and Reset FIFOs
    ; Clear both RX and TX FIFOs and set RX trigger to 14 bytes
    lda #(UART::FCR::ENABLE | UART::FCR::RCVR_RESET | UART::FCR::XMIT_RESET | UART::FCR::TRIGGER_LSB | UART::FCR::TRIGGER_MSB)
    sta HW::UART + UART::REG::FCR

    ; 5. Enable UART Receive Interrupts
    lda #UART::IER::RECEIVE_READY
    sta HW::UART + UART::REG::IER

    ; 6. Modem Control: Enable OUT2 (required for IRQ on many 16C550 boards)
    lda #(UART::MCR::DTR | UART::MCR::RTS_ | UART::MCR::OP2)
    sta HW::UART + UART::REG::MCR

    rts
.endproc

.proc reset_handler
    sei
    cld
    ldx #$FF
    txs

    jsr uart_init   ; Initialize UART
    cli

    jmp woz_start   ; Run WozMon
.endproc

.proc irq_handler
    sei
    php
    pha
    phx

@check_irq:
    ; Check if UART is the source (Interrupt Pending bit is 0 if active)
    lda HW::UART + UART::REG::ISR
    and #(UART::ISR::PENDING | UART::ISR::PRI_2)
    bne @exit           ; If bit 0 is 1, no interrupt pending for UART

@load_char:
    ; Check if Data Ready
    lda HW::UART + UART::REG::LSR_
    and #UART::LSR_::DATA_READY
    beq @exit

    ; Read byte and put in circular buffer
    lda HW::UART + UART::REG::RHR
    jsr uart_buffer_write
    jsr uart_rts_update  ; buffer just grew -- may cross the high water mark

    ; Keep checking in case more bytes are in FIFO
    jmp @load_char

@exit:
    plx
    pla
    plp
    cli
    rti
.endproc

.segment "VECTORS"
    .word $0000         ; NMI
    .word reset_handler ; RESET
    .word irq_handler   ; IRQ