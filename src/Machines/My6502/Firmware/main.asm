.setcpu "65C02"

.ifndef DEBUG_MODE
DEBUG_MODE = 0
.endif

; --- Global Definitions ---
.if DEBUG_MODE = 0

.include "constants.inc"
.include "kernel.s"
.include "wozmon.s"

; --- Custom Applications ---
.segment "BANK1"
.include "shapes.asm"

.else

.include "debug.asm"

.endif

.segment "DEBUGCFG"
.byte $4D, $4F, DEBUG_MODE