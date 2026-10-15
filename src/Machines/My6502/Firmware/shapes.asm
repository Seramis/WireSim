; =============================================================================
; MRDI-1 shapes demo
; =============================================================================
; Draws a border frame, a filled box and a triangle across the full 320x200
; MRDI-1 display in one continuous DRAW_PIXELS stream, then writes a couple
; of lines onto the text layer via WRITE_TEXT/TEXT_ATTR (see
; WRITE_TEXT_DEMO below).
;
; Included by main.asm into the "BANK1" segment (linker.cfg maps BANK1 to
; RAM_B1, $4000-$7FFF), so it's baked into the real kernel ROM at a fixed,
; known address ($4000) but never called automatically -- nothing in
; kernel.s/wozmon.s references DRAW_SHAPES. Run it from a live WozMon
; prompt after boot (sim-run.sh -k, connect over the UART) by typing
; "4000R". The routine ends in RTS, which returns straight to the WozMon
; command loop.
; =============================================================================

; --- Zero-page scratch (avoids kernel.s's $00-$01 and wozmon.s's $24-$30) ---
FULL_ROW    = $40  ; nonzero: this row is part of the top/bottom border
BOX_ACTIVE  = $41  ; nonzero: this row passes through the filled box
TRI_ACTIVE  = $42  ; nonzero: this row passes through the triangle
LBOUND      = $43  ; triangle left edge, in byte-columns, for this row
RBOUND      = $44  ; triangle right edge (exclusive), in byte-columns

; --- Shape geometry (rows in pixels, columns in bytes; 40 bytes/row) ---
; The border is inset one row/byte-column from the physical edge (rather
; than touching it) so there's a visible black margin separating it from
; the display window's own frame when screenshotted -- a border flush
; against the edge is correct but easy to mistake for window chrome.
BORDER_TOP_LO    = 8     ; top band:    rows 8-10
BORDER_TOP_HI    = 11    ; exclusive
BORDER_BOT_LO    = 189   ; bottom band: rows 189-191
BORDER_BOT_HI    = 192   ; exclusive
BORDER_LEFT      = 1     ; byte-column
BORDER_RIGHT     = 38    ; byte-column

BOX_TOP     = 60
BOX_BOTTOM  = 100   ; exclusive
BOX_LEFT    = 15    ; byte-column
BOX_RIGHT   = 25    ; exclusive byte-column

TRI_TOP     = 160
TRI_BOTTOM  = 180   ; exclusive (halfwidth 0..19 over 20 rows)
TRI_CENTER  = 20    ; byte-column

DRAW_SHAPES:
    LDA #MRDI1::OP::DRAW_PIXELS
    STA HW::MRDI1 + MRDI1::REG::IR

    STZ HW::MRDI1 + MRDI1::REG::DR     ; start offset low  = 0
    STZ HW::MRDI1 + MRDI1::REG::DR     ; start offset high = 0

    LDY #0                              ; row 0..199
ROW_LOOP:
    ; --- classify this row once; the column loop below just tests flags ---
    STZ FULL_ROW
    STZ BOX_ACTIVE
    STZ TRI_ACTIVE

    CPY #BORDER_TOP_LO
    BCC NOT_TOP
    CPY #BORDER_TOP_HI
    BCS NOT_TOP
    INC FULL_ROW
NOT_TOP:
    CPY #BORDER_BOT_LO
    BCC NOT_BOTTOM
    CPY #BORDER_BOT_HI
    BCS NOT_BOTTOM
    INC FULL_ROW
NOT_BOTTOM:

    CPY #BOX_TOP
    BCC ROW_NO_BOX
    CPY #BOX_BOTTOM
    BCS ROW_NO_BOX
    INC BOX_ACTIVE
ROW_NO_BOX:

    CPY #TRI_TOP
    BCC ROW_NO_TRI
    CPY #TRI_BOTTOM
    BCS ROW_NO_TRI
    INC TRI_ACTIVE

    TYA
    SEC
    SBC #TRI_TOP                        ; A = halfwidth (0..19)
    STA RBOUND                          ; stash halfwidth here briefly
    LDA #TRI_CENTER
    SEC
    SBC RBOUND
    STA LBOUND                          ; LBOUND = CENTER - halfwidth
    LDA #TRI_CENTER + 1
    CLC
    ADC RBOUND
    STA RBOUND                          ; RBOUND = CENTER + halfwidth + 1
ROW_NO_TRI:

    ; --- emit the 40 bytes (320 pixels) of this row ---
    LDX #0
COL_LOOP:
    LDA FULL_ROW
    BEQ CHECK_BORDER
    LDA #$FF
    BRA HAVE_PIXEL
CHECK_BORDER:
    CPX #BORDER_LEFT                    ; inset left/right byte-columns:
    BEQ IS_BORDER                       ; 1-byte (8px) thick side border
    CPX #BORDER_RIGHT
    BEQ IS_BORDER
    BRA CHECK_BOX
IS_BORDER:
    LDA #$FF
    BRA HAVE_PIXEL
CHECK_BOX:
    LDA BOX_ACTIVE
    BEQ CHECK_TRI
    CPX #BOX_LEFT
    BCC CHECK_TRI
    CPX #BOX_RIGHT
    BCS CHECK_TRI
    LDA #$FF
    BRA HAVE_PIXEL
CHECK_TRI:
    LDA TRI_ACTIVE
    BEQ NO_PIXEL
    CPX LBOUND
    BCC NO_PIXEL
    CPX RBOUND
    BCS NO_PIXEL
    LDA #$FF
    BRA HAVE_PIXEL
NO_PIXEL:
    LDA #$00
HAVE_PIXEL:
    STA HW::MRDI1 + MRDI1::REG::DR      ; stream one pixel byte
    INX
    CPX #40
    BNE COL_LOOP

    INY
    CPY #200
    BEQ ROWS_DONE
    JMP ROW_LOOP
ROWS_DONE:

    JSR WRITE_TEXT_DEMO

    RTS                                  ; back to WozMon's command loop

; =============================================================================
; Text layer demo: WRITE_TEXT + TEXT_ATTR
; =============================================================================
; Text rows 2 and 3 (pixel rows 16-31) sit just below the top border band
; (pixel rows 8-10) and well above the box (pixel row 60+), so the text
; layer has a clear area to draw on. Row 2 is a plain title in white.
; Row 3 is the same length string with every one of its cells' INVERT bit
; set afterwards -- INVERT only recolors a cell's ink from white to black,
; it never fills the cell's (always-transparent) background. DRAW_INVERT_BAND
; below paints a plain white pixel-layer band behind just the *top half*
; of that row (CHAR_HEIGHT/2 pixel rows, not the full cell) so the demo
; stays visible without hiding the point: each glyph's top half sits on
; white and shows its black ink, while its bottom half still sits on
; untouched black with nothing painted behind it -- proof that the
; glyph's own non-ink pixels never draw a background, with or without
; INVERT.

TITLE_ROW   = 2
INVERT_ROW  = 3
TITLE_OFFSET  = MRDI1::TEXT_COLS * TITLE_ROW
INVERT_OFFSET = MRDI1::TEXT_COLS * INVERT_ROW

BYTES_PER_ROW   = MRDI1::WIDTH / 8                       ; 40
BAND_OFFSET     = (INVERT_ROW * MRDI1::CHAR_HEIGHT) * BYTES_PER_ROW ; pixel row 24 -> byte 960

TITLE_TEXT:
    .byte "MRDI-1 TEXT LAYER DEMO"
TITLE_LEN = * - TITLE_TEXT

INVERT_TEXT:
    .byte "REVERSE VIDEO ATTRIBUTE"
INVERT_LEN = * - INVERT_TEXT

WRITE_TEXT_DEMO:
    JSR DRAW_INVERT_BAND

    LDA #MRDI1::OP::WRITE_TEXT
    STA HW::MRDI1 + MRDI1::REG::IR
    LDA #<TITLE_OFFSET
    STA HW::MRDI1 + MRDI1::REG::DR
    LDA #>TITLE_OFFSET
    STA HW::MRDI1 + MRDI1::REG::DR
    LDX #0
WRITE_TITLE_LOOP:
    LDA TITLE_TEXT,X
    STA HW::MRDI1 + MRDI1::REG::DR
    INX
    CPX #TITLE_LEN
    BNE WRITE_TITLE_LOOP

    LDA #MRDI1::OP::WRITE_TEXT
    STA HW::MRDI1 + MRDI1::REG::IR
    LDA #<INVERT_OFFSET
    STA HW::MRDI1 + MRDI1::REG::DR
    LDA #>INVERT_OFFSET
    STA HW::MRDI1 + MRDI1::REG::DR
    LDX #0
WRITE_INVERT_TEXT_LOOP:
    LDA INVERT_TEXT,X
    STA HW::MRDI1 + MRDI1::REG::DR
    INX
    CPX #INVERT_LEN
    BNE WRITE_INVERT_TEXT_LOOP

    LDA #MRDI1::OP::TEXT_ATTR
    STA HW::MRDI1 + MRDI1::REG::IR
    LDA #<INVERT_OFFSET
    STA HW::MRDI1 + MRDI1::REG::DR
    LDA #>INVERT_OFFSET
    STA HW::MRDI1 + MRDI1::REG::DR
    LDX #0
SET_INVERT_ATTR_LOOP:
    LDA #MRDI1::ATTR::INVERT
    STA HW::MRDI1 + MRDI1::REG::DR
    INX
    CPX #INVERT_LEN
    BNE SET_INVERT_ATTR_LOOP

    RTS

; Solid white pixel-layer band, only CHAR_HEIGHT/2 rows tall (half the
; text cell) and the full screen width, starting at BAND_OFFSET. One
; continuous DRAW_PIXELS stream: the VRAM is row-major with no per-row
; padding, so BAND_HEIGHT*BYTES_PER_ROW contiguous bytes from BAND_OFFSET
; exactly cover those pixel rows.
BAND_HEIGHT = MRDI1::CHAR_HEIGHT / 2                     ; 4

DRAW_INVERT_BAND:
    LDA #MRDI1::OP::DRAW_PIXELS
    STA HW::MRDI1 + MRDI1::REG::IR
    LDA #<BAND_OFFSET
    STA HW::MRDI1 + MRDI1::REG::DR
    LDA #>BAND_OFFSET
    STA HW::MRDI1 + MRDI1::REG::DR
    LDY #BAND_HEIGHT
BAND_ROW_LOOP:
    LDX #BYTES_PER_ROW
BAND_COL_LOOP:
    LDA #$FF
    STA HW::MRDI1 + MRDI1::REG::DR
    DEX
    BNE BAND_COL_LOOP
    DEY
    BNE BAND_ROW_LOOP

    RTS
