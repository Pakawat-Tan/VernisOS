; interrupts.asm — ISR/IRQ stubs for VernisOS i686 kernel
; Called by IDT; dispatches to C interrupt_dispatch(InterruptFrame32*)
; x86 cdecl: argument is pushed on stack

[BITS 32]
section .text

; Macro: exception WITHOUT error code
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0        ; dummy error_code
    push dword %1       ; int_no
    jmp isr_common_stub
%endmacro

; Macro: exception WITH error code (CPU already pushed it)
%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1       ; int_no (error_code already on stack from CPU)
    jmp isr_common_stub
%endmacro

; Macro: hardware IRQ
%macro IRQ 2
global irq%1
irq%1:
    push dword 0        ; dummy error_code
    push dword %2       ; int_no (remapped vector)
    jmp isr_common_stub
%endmacro

; ---- CPU Exception stubs (vectors 0-31) ----
ISR_NOERR 0    ; #DE Divide Error
ISR_NOERR 1    ; #DB Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; #BP Breakpoint
ISR_NOERR 4    ; #OF Overflow
ISR_NOERR 5    ; #BR Bound Range
ISR_NOERR 6    ; #UD Invalid Opcode
ISR_NOERR 7    ; #NM Device Not Available
ISR_ERR   8    ; #DF Double Fault
ISR_NOERR 9    ; Coprocessor Overrun
ISR_ERR   10   ; #TS Invalid TSS
ISR_ERR   11   ; #NP Segment Not Present
ISR_ERR   12   ; #SS Stack Fault
ISR_ERR   13   ; #GP General Protection
ISR_ERR   14   ; #PF Page Fault
ISR_NOERR 15   ; Reserved
ISR_NOERR 16   ; #MF x87 FPU Error
ISR_ERR   17   ; #AC Alignment Check
ISR_NOERR 18   ; #MC Machine Check
ISR_NOERR 19   ; #XM SIMD Exception
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; ---- Hardware IRQ stubs (IRQ0-15 → vectors 0x20-0x2F) ----
IRQ  0, 0x20   ; IRQ0  timer
IRQ  1, 0x21   ; IRQ1  PS/2 keyboard
IRQ  2, 0x22
IRQ  3, 0x23
IRQ  4, 0x24
IRQ  5, 0x25
IRQ  6, 0x26
IRQ  7, 0x27
IRQ  8, 0x28
IRQ  9, 0x29
IRQ 10, 0x2A
IRQ 11, 0x2B
IRQ 12, 0x2C
IRQ 13, 0x2D
IRQ 14, 0x2E
IRQ 15, 0x2F

; ---- INT 0x80 syscall ----
global isr_syscall
isr_syscall:
    push dword 0        ; dummy error_code
    push dword 0x80     ; int_no
    jmp isr_common_stub

; ---- Common stub ----
; Stack layout when we enter here (relative to ESP):
;   [ESP+0]  int_no         (pushed by ISR_* macro or isr_syscall)
;   [ESP+4]  error_code     (pushed by CPU or dummy 0)
;   [ESP+8]  EIP            (pushed by CPU)
;   [ESP+12] CS             (pushed by CPU)
;   [ESP+16] EFLAGS         (pushed by CPU)
;   [ESP+20] ESP_old        (pushed by CPU if privilege change)
;   [ESP+24] SS             (pushed by CPU if privilege change)

isr_common_stub:
    pusha               ; EAX ECX EDX EBX ESP(dummy) EBP ESI EDI → stack
    push ds
    push es
    push fs
    push gs

    ; Reload kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Push pointer to frame (cdecl argument for interrupt_dispatch)
    push esp

    extern interrupt_dispatch
    call interrupt_dispatch

    add esp, 4          ; remove pushed argument (cdecl cleanup)

    ; Phase 18: context switch — if EAX != 0, switch to new task's stack
    test eax, eax
    jz .no_ctx_switch
    mov esp, eax
.no_ctx_switch:

    pop gs
    pop fs
    pop es
    pop ds

    popa

    add esp, 8          ; remove int_no and error_code we pushed

    iret

; Mark stack as non-executable
section .note.GNU-stack noalloc noexec nowrite progbits
