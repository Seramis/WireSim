.org $C100

START:
    ;; 1. Minimal architectural validation
    LDA #$AA        ; Load immediate pattern 1
    LDX #$55        ; Load immediate pattern 2
    TXA             ; Transfer X to A (Tests internal register paths)
    SEC             ; Set Carry (Tests explicit flag manipulation)

    ;; 2. Terminate and dump state via Stack
    BRK             ; Trigger software interrupt (Tests PC/P pushing)
    NOP             ; BRK skipping byte
DONE:
    JMP DONE        ; Park after BRK returns so execution can't fall into NMI/RTI

NMI:
    RTI

IRQ:
    RTI

.segment "VECTORS"
    .word NMI     ; NMI
    .word START   ; RESET
    .word IRQ     ; IRQ