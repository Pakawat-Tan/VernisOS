;
;  Flow bootloader diagram Stage 3
;
; ===================================================================================================================
; |  เริ่มต้น bootloader (real mode)                                                                                   |
; |  ├── เคลียร์ Segment Registers และ Stack Pointer                                                                  |
; |  ├── แสดงข้อความ "Stage 3 - 64-bit Kernel Loader" ผ่าน BIOS INT 10h                                               |
; |  └── ตรวจสอบว่า A20 line ถูกเปิดใช้งานหรือยัง                                                                         |
; |        ├── [Enabled] → ดำเนินการต่อ                                                                               |
; |        └── [Disabled] → พยายามเปิด A20 หากยังล้มเหลว → พิมพ์ “A20 Error” แล้ว halt                                    |
; |  โหลด kernel (โดยใช้ LBA หาก BIOS รองรับ, ไม่งั้น fallback เป็น CHS)                                                  |
; |  ├── [INT 13h Extensions Available] → โหลด kernel_x64 จาก sector 6 เป็นต้นไป                                      |
; |  └── [Fallback to CHS] → โหลด kernel แบบ CHS จาก cylinder/head/sector ที่กำหนดไว้                                  |
; |        ├── [Success] → ไปต่อ                                                                                     |
; |        └── [Fail] → แสดง “Disk read error!” แล้ว halt                                                            |
; |  เตรียมระบบ Memory Paging สำหรับ Long Mode                                                                        |
; |  ├── Clear page table memory ที่ 0x1000, 0x2000, 0x3000                                                           |
; |  ├── สร้าง PML4, PDPT, PD สำหรับ map 0x00000000–0x00200000 ด้วย 2MB page                                           |
; |  ├── เปิด PAE (CR4.PAE = 1)                                                                                      |
; |  └── เปิด Long Mode ผ่าน MSR EFER (bit LME)                                                                       |
; |  สร้าง GDT ใหม่, เปิด Protected Mode และ Paging (CR0 |= PG|PE)                                                     |
; |  └── Far Jump ไปยัง long_mode_start เพื่อเข้าสู่ 64-bit mode                                                          |
; |  [64-bit Mode]                                                                                                  |
; |  ├── Set segment registers (DS, SS = 0)                                                                         |
; |  ├── เคลียร์หน้าจอ, แสดง “Entering 64-bit mode...”                                                                 |
; |  ├── Copy kernel (โหลดไว้ที่ 0x10000) ไปยัง 0x100000                                                                |
; |  └── Jump ไปยัง entry point ที่ 0x100000                                                                           |
; ===================================================================================================================
;
;
;  ตารางที่อยู่ (Address Table) สำหรับ Stage 3 Bootloader (x86_64)
;
; ==========================================================================================================================================================================
; Address           | Bytes | Instruction                 | Operands/Value             | Description                                                                       |
;-------------------|-------|-----------------------------|----------------------------|-----------------------------------------------------------------------------------|
;| 0x90000          |       | -                           | -                          | จุดเริ่มต้นของ Stage 3 ที่โหลดโดย Stage 2                                               |
;| 0x90000          | 1     | `cli`                       | -                          | ปิด interrupt                                                                      |
;| 0x90001          | 2     | `xor`                       | `ax, ax`                   | เคลียร์ AX                                                                          |
;| 0x90003–0x9000D  | ~11   | `mov`                       | เซต DS, ES, SS, SP         | เตรียม environment เริ่มต้น                                                           |
;| 0x9000E          | 5     | `call print_message`        | `si = stage3_msg`          | แสดงข้อความเริ่มต้น                                                                   |
;| 0x90013          | ...   | `call check_a20`            | -                          | ตรวจสอบว่า A20 เปิดหรือไม่                                                            |
;| 0x90020          | ...   | `call enable_a20`           | -                          | หากยังไม่เปิด → พยายามเปิด                                                            |
;| 0x90030          | ...   | `load_kernel_64:`           | -                          | ตรวจสอบว่า BIOS รองรับ INT 13h Extensions หรือไม่                                     |
;| 0x90035–...      | ...   | `int 0x13`                  | EXT or CHS read sectors    | โหลด kernel จาก disk                                                              |
;| 0x90080          | ...   | `setup_long_mode:`          | -                          | สร้าง page tables ที่ 0x1000–0x3000                                                  |
;| 0x90090          | ...   | `mov cr4, ...`              | เปิด PAE                    | เปิด CR4.PAE                                                                       |
;| 0x900A0          | ...   | `wrmsr`                     | MSR_EFER ← LME             | เปิด Long Mode (MSR 0xC0000080, bit 8)                                             |
;| 0x900B0          | ...   | `lgdt [gdt_descriptor]`     | -                          | โหลด GDT ใหม่                                                                      |
;| 0x900C0          | 5     | `jmp 0x08:long_mode_start`  | Far Jump                   | กระโดดไปยัง long_mode_start (64-bit mode)                                          |
;| 0x90100          |       | `long_mode_start:`          | -                          | จุดเริ่มต้น 64-bit code                                                               |
;| 0x90100+         | ...   | `mov rax, 0x10000`          | src (kernel temp)          | เตรียม pointer เพื่อ copy kernel                                                     |
;| 0x90110+         | ...   | `mov rdi, 0x100000`         | dst (kernel final)         | ชี้ไปยังตำแหน่งปลายทาง                                                                |
;| 0x90120+         | ...   | `rep movsq`                 | copy                       | คัดลอก kernel ไปยังตำแหน่ง 1MB                                                       |
;| 0x90140          | ...   | `jmp 0x100000`              | jump to kernel entry       | กระโดดไปยัง Kernel                                                                 |
;| 0x90200+         | ~256  | `db`                        | ข้อความสำหรับ BIOS print     | เช่น "Stage 3", "A20 Error", "Disk read error!"                                    |
; ==========================================================================================================================================================================
;
; =====================================================================================================================
; Bootloader Stage 3: ตรวจสอบและเปิดใช้งาน A20 Line และโหลด Kernel 64-bit
; =====================================================================================================================
;
[BITS 16]
[ORG 0x9000]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x8000
    sti

    call clear_screen

    mov si, stage3_msg
    call print_message

    ; ตรวจสอบและเปิดใช้งาน A20
    call check_a20
    test ax, ax
    jnz a20_enabled
    call enable_a20
    call check_a20
    test ax, ax
    jz a20_failed

a20_enabled:
    mov si, a20_ok_msg
    call print_message

    ; โหลด kernel 64-bit
    call load_kernel_64
    test ax, ax
    jz kernel_load_failed

    ; Setup และเข้าสู่ long mode
    call setup_long_mode
    call enter_long_mode

a20_failed:
    mov si, a20_fail_msg
    call print_message
    jmp hang

kernel_load_failed:
    mov si, kernel_fail_msg
    call print_message
    jmp hang

; =======================================================================
; Clear Screen Function
; =======================================================================
clear_screen:
    pusha
    mov ax, 0x0003
    int 0x10
    popa
    ret

; =======================================================================
; Load 64-bit Kernel
; =======================================================================
load_kernel_64:
    mov si, loading_kernel_msg
    call print_message

   ; ใช้ BIOS extensions
    mov ah, 0x41
    mov bx, 0x55aa
    mov dl, 0x80
    int 0x13
    jc use_chs_kernel

    ; Use LBA
    mov si, disk_packet
    mov byte [si], 0x10
    mov byte [si+1], 0
    mov word [si+2], 16      ; 16 sectors for kernel
    mov word [si+4], 0
    mov word [si+6], 0x1000  ; Load to 0x10000
    mov dword [si+8], 30      ; LBA sector 30
    mov dword [si+12], 0

    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc load_error
    ret

use_chs_kernel:
    ; Use CHS
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    ; CHS
    mov ah, 0x02
    mov al, 16              ; โหลด 16 sectors
    mov ch, 0               ; cylinder 0
    mov cl, 31              ; sector 31 (CHS sector เริ่มที่ 1)
    mov dh, 30              ; head 30
    mov dl, 0x80            ; drive 0x80
    int 0x13
    jc load_error

load_error:
    xor ax, ax
    ret

; =======================================================================
; A20 Line Functions
; =======================================================================
check_a20:
    pushf
    push ds
    push es
    push di
    push si

    cli
    xor ax, ax
    mov es, ax
    mov di, 0x0500
    mov ax, 0xFFFF
    mov ds, ax
    mov si, 0x0510

    mov al, byte [es:di]
    push ax
    mov al, byte [ds:si]
    push ax

    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF

    cmp byte [es:di], 0xFF

    pop ax
    mov byte [ds:si], al
    pop ax
    mov byte [es:di], al

    mov ax, 0
    je .exit
    mov ax, 1

.exit:
    pop si
    pop di
    pop es
    pop ds
    popf
    ret

enable_a20:
    ; Method 1: Fast A20
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; =======================================================================
; Setup Long Mode
; =======================================================================
setup_long_mode:
    mov si, setup_longmode_msg
    call print_message

    ; ปิด interrupts
    cli

    ; เคลียร์ page tables area (0x1000-0x5000)
    mov si, debug_clearing_msg
    call print_message
    
    ; เคลียร์หน่วยความจำสำหรับ page tables
    push es
    push di
    mov ax, 0x0100      ; 0x1000 segment
    mov es, ax
    xor di, di
    mov cx, 0x2000      ; 16KB
    xor ax, ax
    cld
    rep stosw
    pop di
    pop es

    ; Setup page tables
    mov si, debug_paging_msg
    call print_message

    ; PML4 Table at 0x1000
    mov dword [0x1000], 0x2003  ; PML4[0] -> PDPT at 0x2000
    mov dword [0x1004], 0

    ; PDPT at 0x2000
    mov dword [0x2000], 0x3003  ; PDPT[0] -> PD at 0x3000
    mov dword [0x2004], 0

    ; PD at 0x3000 (2MB pages)
    mov dword [0x3000], 0x000083  ; 0-2MB: Present, Writable, 2MB page
    mov dword [0x3004], 0
    mov dword [0x3008], 0x200083  ; 2-4MB: Present, Writable, 2MB page
    mov dword [0x300C], 0

    ; Load page table base
    mov eax, 0x1000
    mov cr3, eax

    ; Enable PAE
    mov si, debug_pae_msg
    call print_message
    mov eax, cr4
    or eax, 1 << 5      ; PAE bit
    mov cr4, eax

    ; Enable Long Mode in EFER
    mov si, debug_efer_msg
    call print_message
    mov ecx, 0xC0000080 ; EFER MSR
    rdmsr
    or eax, 1 << 8      ; LME bit
    wrmsr

    mov si, longmode_setup_ok_msg
    call print_message
    ret

; =======================================================================
; Enter Long Mode
; =======================================================================
enter_long_mode:
    mov si, entering_longmode_msg
    call print_message

    cli
    
    ; Load GDT
    lgdt [gdt_descriptor]
    
    ; เข้าสู่ protected mode และเปิด paging พร้อมกัน
    mov eax, cr0
    or eax, 0x80000001  ; Enable PE bit (bit 0) and PG bit (bit 31)
    mov cr0, eax

    ; Far jump เพื่อเข้าสู่ 64-bit mode
    jmp 0x08:long_mode_start

; =======================================================================
; 64-bit Long Mode Code
; =======================================================================
[BITS 64]
long_mode_start:
    ; Setup 64-bit segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; ตั้งค่า stack ให้อยู่ในตำแหน่งที่ปลอดภัย
    mov rsp, 0x7E00     ; ใช้ stack ที่ต่ำกว่า bootloader

    ; แสดงข้อความว่าเข้าสู่ long mode แล้ว
    mov rsi, longmode_entered_msg
    call print_64bit_message

    ; เอาการตรวจสอบ kernel ออก - ไม่จำเป็น
    ; Copy kernel โดยตรง
    mov rsi, kernel_copy_msg
    call print_64bit_message
    
    ; Copy kernel จาก 0x10000 ไปยัง 0x100000 (1MB mark)
    mov rsi, 0x10000
    mov rdi, 0x100000
    mov rcx, (16 * 512) / 8       ; 16 sectors * 512 bytes / 8 bytes per qword
    cld
    rep movsq

    ; แสดงข้อความก่อน jump
    mov rsi, jumping_kernel_msg
    call print_64bit_message
    
    ; รอสักหน่อยเพื่อให้เห็นข้อความ
    mov rcx, 0x1000000
.delay_loop:
    dec rcx
    jnz .delay_loop

    ; Jump ไปยัง kernel entry point
    ; ใช้ absolute jump แทน relative jump
    mov rax, 0x100000
    jmp rax

kernel_not_found:
    ; แสดงข้อความ error และ hang
    mov rsi, no_kernel_msg
    call print_64bit_message
    cli
    hlt

; ฟังก์ชันแสดงข้อความใน 64-bit mode
print_64bit_message:
    push rax
    push rbx
    push rcx
    push rdx
    
    mov rdx, 0xB8000    ; VGA buffer
    xor rax, rax        ; position counter
    
.print_loop:
    mov bl, byte [rsi]  ; load character
    test bl, bl
    jz .print_done
    
    mov byte [rdx + rax*2], bl       ; character
    mov byte [rdx + rax*2 + 1], 0x0F ; white on black
    
    inc rax
    inc rsi
    jmp .print_loop
    
.print_done:
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

; =======================================================================
; 16-bit Functions
; =======================================================================
[BITS 16]
hang:
    mov si, hang_msg
    call print_message
    cli
    hlt
    jmp hang

print_message:
    pusha
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

; =======================================================================
; Global Descriptor Table for Long Mode
; =======================================================================
align 16
gdt_start:
    ; Null descriptor
    dq 0x0000000000000000
    
    ; 64-bit code segment (selector 0x08)
    dq 0x00209A0000000000
    
    ; 64-bit data segment (selector 0x10)
    dq 0x0000920000000000
    
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dq gdt_start

; =======================================================================
; Data Section
; =======================================================================
disk_packet: times 16 db 0

; =======================================================================
; Messages
; =======================================================================
stage3_msg              db "Stage 3 - 64-bit Kernel Loader", 13, 10, 0
loading_kernel_msg      db "Loading 64-bit kernel...", 13, 10, 0
kernel_loaded_msg       db "Kernel loaded successfully", 13, 10, 0
kernel_fail_msg         db "Failed to load kernel!", 13, 10, 0
a20_ok_msg             db "A20 line enabled", 13, 10, 0
a20_fail_msg           db "Failed to enable A20 line!", 13, 10, 0
setup_longmode_msg     db "Setting up long mode...", 13, 10, 0
longmode_setup_ok_msg  db "Long mode setup complete", 13, 10, 0
entering_longmode_msg  db "Entering 64-bit long mode...", 13, 10, 0
hang_msg               db "System halted", 13, 10, 0

; 64-bit messages (null-terminated)
longmode_entered_msg   db "64-bit Long Mode Active!", 0
no_kernel_msg          db "No kernel found!", 0

debug_clearing_msg      db "Clearing page tables...", 13, 10, 0
debug_paging_msg        db "Setting up page tables...", 13, 10, 0
debug_pae_msg           db "Enabling PAE...", 13, 10, 0
debug_efer_msg          db "Enabling long mode...", 13, 10, 0

kernel_copy_msg        db "Copying kernel to 1MB...", 10, 0
jumping_kernel_msg     db "Jumping to kernel...", 10, 0