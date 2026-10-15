; I/O Definitions
.scope HW
    IO = $C000

    UART      = IO + 0      ; $C000-$C007, 16C550 UART
    REG8      = IO + 8      ; $C008, Register8 (Verilog 8-bit transparent latch)
    DBGWRITER = IO + 9      ; $C009, DebugWriter (write-only debug sink)
    MRDI1     = IO + $0A    ; $C00A-$C00B, MRDI-1 monochrome raster display
    LEDSWITCHBOX = IO + $0C ; $C00C, LedSwitchBox (8-bit LED/switch I/O port)

    .scope BANK ; TODO
        _BASE   = IO + $10

        BANK0   = _BASE + 0 ; RW
        BANK1   = _BASE + 1 ; RW
        BANK1W  = _BASE + 2 ; Write override
        BANK2   = _BASE + 3 ; RW
        BANK2W  = _BASE + 4 ; Write override
    .endscope
.endscope

; =============================================================================
; 16C550 UART DEFINITIONS
; =============================================================================
.scope UART
    ; Register Offsets
    .scope REG
        RHR     = 0 ; R - Receive Holding Register
        THR     = 0 ; W - Transmit Holding Register
        DLL     = 0 ; RW - LSB of Divisor Latch (DLAB=1)
        IER     = 1 ; RW - Interrupt Enable Register
        DLM     = 1 ; RW - MSB of Divisor Latch (DLAB=1)
        ISR     = 2 ; R - Interrupt Status Register
        FCR     = 2 ; W - FIFO Control Register
        LCR     = 3 ; RW - Line Control Register
        MCR     = 4 ; RW - Modem Control Register
        LSR_    = 5 ; R - Line Status Register
        MSR     = 6 ; R - Modem Status Register
        SCR     = 7 ; RW - Scratchpad Register
    .endscope

    ; Interrupt Enable Register (IER) Bits
    .scope IER
        RECEIVE_READY   = 1 << 0 ; Enable Receiver Ready Interrupt
        TRANSMIT_EMPTY  = 1 << 1 ; Enable Transmitter Empty Interrupt
        LINE_STATUS     = 1 << 2 ; Enable Receiver Line Status Interrupt
        MODEM_STATUS    = 1 << 3 ; Enable Modem Status Register Interrupt
    .endscope

    ; FIFO Control Register (FCR) Bits
    .scope FCR
        ENABLE          = 1 << 0 ; Enable Transmit and Receive FIFO
        RCVR_RESET      = 1 << 1 ; Reset Receiver FIFO
        XMIT_RESET      = 1 << 2 ; Reset Transmitter FIFO
        DMA_MODE        = 1 << 3 ; DMA Mode Select
        TRIGGER_LSB     = 1 << 6 ; RCVR Trigger LSB
        TRIGGER_MSB     = 1 << 7 ; RCVR Trigger MSB
    .endscope

    ; Interrupt Status Register (ISR) Bits
    .scope ISR
        PENDING         = 1 << 0 ; 0 = Interrupt Pending
        PRI_0           = 1 << 1 ; Interrupt Priority Bit 0
        PRI_1           = 1 << 2 ; Interrupt Priority Bit 1
        PRI_2           = 1 << 3 ; Interrupt Priority Bit 2
        FIFO_EN_0       = 1 << 6 ; FIFO Enabled Bit 0
        FIFO_EN_1       = 1 << 7 ; FIFO Enabled Bit 1
    .endscope

    ; Line Control Register (LCR) Bits
    .scope LCR
        WORD_LEN_0      = 1 << 0 ; Word Length Select Bit 0
        WORD_LEN_1      = 1 << 1 ; Word Length Select Bit 1
        STOP_BITS       = 1 << 2 ; Stop Bit Select
        PARITY_ENABLE   = 1 << 3 ; Parity Enable
        EVEN_PARITY     = 1 << 4 ; Even Parity Select
        SET_PARITY      = 1 << 5 ; Set Parity
        SET_BREAK       = 1 << 6 ; Set Break
        DLAB            = 1 << 7 ; Divisor Latch Access Bit
    .endscope

    ; Modem Control Register (MCR) Bits
    .scope MCR
        DTR             = 1 << 0 ; Data Terminal Ready
        RTS_            = 1 << 1 ; Request to Send
        OP1             = 1 << 2 ; Output 1
        OP2             = 1 << 3 ; Output 2
        LOOPBACK        = 1 << 4 ; Internal Loopback Mode
    .endscope

    ; Line Status Register (LSR) Bits
    .scope LSR_
        DATA_READY      = 1 << 0 ; Receiver Data Ready
        OVERRUN_ERR     = 1 << 1 ; Overrun Error
        PARITY_ERR      = 1 << 2 ; Parity Error
        FRAMING_ERR     = 1 << 3 ; Framing Error
        BREAK_INT       = 1 << 4 ; Break Interrupt
        TH_EMPTY        = 1 << 5 ; Transmit Holding Register Empty
        TRANS_EMPTY     = 1 << 6 ; Transmitter Empty
        FIFO_DATA_ERR   = 1 << 7 ; Receiver FIFO Data Error
    .endscope

    ; Modem Status Register (MSR) Bits
    .scope MSR
        DELTA_CTS       = 1 << 0 ; Delta Clear to Send
        DELTA_DSR       = 1 << 1 ; Delta Data Set Ready
        DELTA_RI        = 1 << 2 ; Trailing Edge Ring Indicator
        DELTA_CD        = 1 << 3 ; Delta Carrier Detect
        CTS             = 1 << 4 ; Clear to Send
        DSR             = 1 << 5 ; Data Set Ready
        RI              = 1 << 6 ; Ring Indicator
        CD              = 1 << 7 ; Carrier Detect
    .endscope
.endscope

; =============================================================================
; MRDI-1 MONOCHROME RASTER DISPLAY DEFINITIONS
; =============================================================================
; 320x200 1bpp framebuffer, MSB-first within each byte, plus a 40x25
; character text layer always composited in front of it. Two write-only
; registers at HW::MRDI1 (IR at offset REG::IR, DR at offset REG::DR); see
; src/Devices/Graphics/Mrdi1/Mrdi1.hpp/.md for the streaming protocol shared by
; DRAW_PIXELS/WRITE_TEXT/TEXT_ATTR.
.scope MRDI1
    ; Register Offsets (add to HW::MRDI1)
    .scope REG
        IR = 0 ; W - Instruction Register: opcode, resets the FSM
        DR = 1 ; W - Data Register: sequential parameter/pixel-stream bytes
    .endscope

    ; Instruction Register (IR) Opcodes
    .scope OP
        DRAW_PIXELS = $00 ; addr-lo, addr-hi, then an auto-incrementing pixel stream
        WRITE_TEXT  = $01 ; addr-lo, addr-hi, then an auto-incrementing character stream
        TEXT_ATTR   = $02 ; addr-lo, addr-hi, then an auto-incrementing attribute stream
    .endscope

    ; TEXT_ATTR bits (one byte per character cell)
    .scope ATTR
        INVERT = 1 << 0 ; reverse video: opaque cell, ink/background swapped
    .endscope

    WIDTH     = 320
    HEIGHT    = 200
    VRAM_SIZE = 8000 ; (WIDTH * HEIGHT) / 8

    CHAR_WIDTH  = 8
    CHAR_HEIGHT = 8
    TEXT_COLS   = 40  ; WIDTH / CHAR_WIDTH
    TEXT_ROWS   = 25  ; HEIGHT / CHAR_HEIGHT
    TEXT_SIZE   = 1000 ; TEXT_COLS * TEXT_ROWS
.endscope