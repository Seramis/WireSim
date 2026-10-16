; Lays out the 32 KiB AT28C256 image ($0000-$7FFF): every byte starts erased
; ($FF), the image then overwrites what it defines. Include first in an image,
; end the image with: savebin ROM_BIN, 0x0000, 0x8000
; The build passes -DROM_BIN=<output path>.

	device NOSLOT64K
	org 0x0000
	ds 0x8000, 0xFF
