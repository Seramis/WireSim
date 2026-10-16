; Hardware map shared by the firmware images (see ../AGENTS.md, "Machine facts").

; ST16C550 UART in I/O space, ports $80-$87 (register select on A0-A2).
UART_BASE   equ 0x80
UART_RBR    equ UART_BASE + 0   ; read: receive buffer (DLAB=0)
UART_THR    equ UART_BASE + 0   ; write: transmit holding (DLAB=0)
UART_DLL    equ UART_BASE + 0   ; divisor latch low (DLAB=1)
UART_IER    equ UART_BASE + 1   ; interrupt enable (DLAB=0)
UART_DLM    equ UART_BASE + 1   ; divisor latch high (DLAB=1)
UART_ISR    equ UART_BASE + 2   ; read: interrupt status
UART_FCR    equ UART_BASE + 2   ; write: FIFO control
UART_LCR    equ UART_BASE + 3   ; line control
UART_MCR    equ UART_BASE + 4   ; modem control
UART_LSR    equ UART_BASE + 5   ; line status
UART_MSR    equ UART_BASE + 6   ; modem status
UART_SPR    equ UART_BASE + 7   ; scratch

LCR_DLAB    equ 0x80
LCR_8N1     equ 0x03
FCR_ENABLE  equ 0x01            ; enable FIFOs
FCR_RX_CLR  equ 0x02            ; clear the receive FIFO
IER_RX      equ 0x01            ; interrupt on received data
MCR_DTR     equ 0x01
MCR_RTS     equ 0x02            ; clear-to-send for the host end of the line
MCR_OP2     equ 0x08
MCR_LOOP    equ 0x10
LSR_DR      equ 0            ; bit numbers, for BIT
LSR_THRE    equ 5

; Memory: 32 KiB EEPROM $0000-$7FFF, 32 KiB SRAM $8000-$FFFF.
RAM_BASE    equ 0x8000
