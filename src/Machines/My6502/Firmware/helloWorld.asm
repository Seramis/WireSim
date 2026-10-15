.setcpu "65C02"

.segment "ZP"
    DATA_BUFFER:
        .res 1 ; Reserve 1 byte at $0000

.segment "CODE"

RESET:
    LDX #$FF                ; Initialize stack pointer
    TXS

POLL_LOOP:
    LDA $FF00               ; Load value from trigger location
    BEQ POLL_LOOP           ; If value is 0, keep checking

    ; Process value (Value is currently in Accumulator)
    STA $FF01               ; Write to target memory location
    STA DATA_BUFFER         ; Write to Zero Page ($0000)

    ; Clear trigger
    LDA #$00
    STA $FF00               ; Reset trigger location to 0x00

    JMP POLL_LOOP           ; Repeat endlessly

.segment "VECTORS"
.word 0000                  ; NMI Vector
.word RESET                 ; Reset Vector (Points to start of code)
.word 0000                  ; IRQ Vector