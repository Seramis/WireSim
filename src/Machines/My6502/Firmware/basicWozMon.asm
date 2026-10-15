; wozmon.asm
;
; Originally from Apple-1 Operation Manual, Steve Wozniak, 1976
; Revised 2024 May 8 for Commodore 64/VIC/128 by David R. Van Wagner davevw.com
; * Using C64 KERNAL (instead of MC6520 and KBD/CRT)
; * extra processing for expected mark parity, software caps lock, and revised newline/carriage return processing
; * revised to expect terminal line edit mode instead of echo off character processing
; * revised to acme syntax
; * different zero page usage
; * changed l/h to wl/wh because vice didn't like that symbol
; * reverse toggle instead of spaces only on vic-20 (like HESMON) because too few columns

ASCII_CR = $0D
ASCII_BACKSPACE = $08
ASCII_ESC = $1B
ASCII_BACKLASH = $5C
ASCII_PERIOD = $2E
ASCII_COLON = $3A
ASCII_SPACE = $20

TERM_IO=$C008

; zero page usage - tape stuff on vic-20, 64, 128.  Needs to change for PET, TED, Plus/4, 16, etc.
xaml=$a3
xamh=$a4
stl=$a5
sth=$a6
wl=$a7
wh=$a8
ysav=$a9
mode=$aa

in=$200 ; same as Commodore uses, should be fine to copy from/to this, will probably use slightly less
;** C64 etc. support added by David R. Van Wagner davevw.com ***************************************

.segment "CODE"

START:
	cld
	cli
	jmp escape

;** C64, etc. support added by David R. Van Wagner davevw.com ***************************************
KBD_IN:
	;sty $22
	;jsr CHRIN ; note: full screen editor
	;ldy $22
    lda TERM_IO
    beq KBD_IN
	rts
;** C64, etc. support added by David R. Van Wagner davevw.com ***************************************

notcr:
	cmp #ASCII_BACKSPACE + $80 ; underscore or Commodore back arrow (rub out?)
	beq backspace
	cmp #ASCII_ESC + $80
	beq escape
	iny
	bpl nextchar
escape:
	lda #ASCII_BACKLASH + $80 ; backslash
	jsr echo
getline:
	lda #ASCII_CR
	jsr echo
	ldy #1
backspace:
	dey
	bmi getline
nextchar:
	jsr KBD_IN
	ora #$80
	sta in, y
	jsr echo ; - needed only if terminal echo off, line editing off
	cmp #ASCII_CR + $80
	bne notcr
	ldy #$ff
	lda #$00
	tax
setstor:
	asl
setmode:
	sta mode
blskip:
	iny
nextitem:
	lda in, y
	cmp #ASCII_CR + $80
	beq getline
	cmp #ASCII_PERIOD + $80 ; period
	bcc blskip
	beq setmode
	cmp #ASCII_COLON + $80 ; colon
	beq setstor
	cmp #'R' + $80 ; R
	beq run
	stx wl
	stx wh
	sty ysav
nexthex:
	lda in, y
	eor #$B0
	cmp #$0A
	bcc dig
	adc #$88
	cmp #$FA
	bcc nothex
dig:
	asl
	asl
	asl
	asl
	ldx #4
hexshift:
	asl
	rol wl
	rol wh
	dex
	bne hexshift
	iny
	bne nexthex
nothex:
	cpy ysav
	beq escape
	bit mode
	bvc notstor
	lda wl
	sta (stl, x)
	inc stl
	bne nextitem
	inc sth
tonextitem:
	jmp nextitem
run:
	jmp (xaml)
notstor:
	bmi xamnext
	ldx #2
setadr:
	lda wl-1,x
	sta stl-1,x
	sta xaml-1,x
	dex
	bne setadr
nxtprnt:
	bne prdata
	lda #ASCII_CR
	jsr echo
	lda xamh
	jsr prbyte
	lda xaml
	jsr prbyte
	lda #ASCII_COLON + $80 ; colon
	jsr echo
prdata:
	lda #ASCII_SPACE + $80
	jsr echo
	lda (xaml,x)
	jsr prbyte
xamnext:
	stx mode
	lda xaml
	cmp wl
	lda xamh
	sbc wh
	bcs tonextitem
	inc xaml
	bne mod8chk
	inc xamh
mod8chk:
	lda xaml
	and #7
	bpl nxtprnt ; should always branch
prbyte:
	pha
	lsr
	lsr
	lsr
	lsr
	jsr prhex
	pla
prhex:
	and #$0F
	ora #$B0
	cmp #ASCII_COLON + $80
	bcc echo
	adc #6
echo:
;** C64, etc. support added by David R. Van Wagner davevw.com ***************************************
	and #$7f ; strip mark bit
    sta TERM_IO
    ora #$80
;	cmp #32	; space?
;	bne notspace
;	lda $FF80 ; Commodore ROM version
;	cmp #$16  ; VIC?
;	bne notvic
;	lda 199
;	eor #18	; invert reverse state
;	sta 199
	rts
;notvic:
;    lda #32
;notspace:
;	jmp CHROUT
;** C64, etc. support added by David R. Van Wagner davevw.com ***************************************

dbg:
    pha
    lda #'!'
    jsr echo
    pla
    rts

.segment "VECTORS"
.word 0000                  ; NMI Vector
.word START                 ; Reset Vector (Points to start of code)
.word 0000                  ; IRQ Vector