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

    ; --- Detect CPU long-mode support via CPUID ---
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .x86_path                    ; no extended CPUID → 32-bit CPU
    mov eax, 0x80000001
    cpuid
    bt edx, 29                      ; bit 29 = Long Mode
    jnc .x86_path

    ; 64-bit path: load x64 kernel from sector 2048
    call load_kernel_64
    test ax, ax
    jz kernel_load_failed
    call vbe_setup                  ; VBE framebuffer mode switch (real mode)
    call setup_long_mode            ; page tables + FB mapping
    call enter_long_mode
    jmp hang                        ; should not reach here

.x86_path:
    ; 32-bit path: load x86 kernel from sector 12
    call load_kernel_86
    test ax, ax
    jz kernel_load_failed
    call vbe_setup                  ; VBE framebuffer mode switch (real mode)
    call enter_protected_mode_32
    jmp hang                        ; should not reach here

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

    ; Use LBA - load kernel in chunks of 128 sectors each
    ; 128 sectors * 512 bytes = 65536 bytes = 64KB = exactly one segment
    ; Physical address layout (no gaps between chunks):
    ;   Chunk 1:  LBA 1024-1151  → 0x10000-0x1FFFF
    ;   Chunk 2:  LBA 1152-1279  → 0x20000-0x2FFFF
    ;   ...
    ;   Chunk 12: LBA 2432-2559  → 0xC0000-0xCFFFF
    ; Total: 12*128 = 1536 sectors = 768KB (covers kernels up to ~768KB)
    ; NOTE: x64 kernel starts at sector 2048 to avoid overlap with x86 (may exceed 1024 sectors)
    mov word [lba_cur], 2048    ; Starting LBA
    mov word [seg_cur], 0x1000  ; Starting segment (physical 0x10000)
    mov word [chunks], 12       ; 12 full chunks of 128 sectors = 1536 sectors total

.load_loop:
    mov si, disk_packet
    mov byte [si], 0x10
    mov byte [si+1], 0
    mov word [si+2], 128        ; 128 sectors = 64KB exactly
    mov word [si+4], 0          ; offset 0
    mov ax, [seg_cur]
    mov word [si+6], ax         ; segment
    xor eax, eax
    mov ax, [lba_cur]
    mov dword [si+8], eax       ; LBA low 32 bits
    mov dword [si+12], 0        ; LBA high 32 bits

    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc load_error

    add word [lba_cur], 128
    add word [seg_cur], 0x1000

    dec word [chunks]
    jnz .load_loop

    ; All 12 full chunks loaded (1536 sectors = 768KB), no partial chunk needed
    mov ax, 1
    ret

lba_cur dw 0
seg_cur dw 0
chunks  dw 0

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

    ; PD at 0x3000 (2MB pages) - map first 8MB
    mov dword [0x3000], 0x000083  ; 0-2MB:   Present, Writable, 2MB page
    mov dword [0x3004], 0
    mov dword [0x3008], 0x200083  ; 2-4MB:   Present, Writable, 2MB page
    mov dword [0x300C], 0
    mov dword [0x3010], 0x400083  ; 4-6MB:   Present, Writable, 2MB page
    mov dword [0x3014], 0
    mov dword [0x3018], 0x600083  ; 6-8MB:   Present, Writable, 2MB page
    mov dword [0x301C], 0
    mov dword [0x3020], 0x800083  ; 8-10MB:  Present, Writable, 2MB page
    mov dword [0x3024], 0
    mov dword [0x3028], 0xA00083  ; 10-12MB: Present, Writable, 2MB page
    mov dword [0x302C], 0
    mov dword [0x3030], 0xC00083  ; 12-14MB: Present, Writable, 2MB page
    mov dword [0x3034], 0
    mov dword [0x3038], 0xE00083  ; 14-16MB: Present, Writable, 2MB page
    mov dword [0x303C], 0

    ; --- Map framebuffer in page tables (if VBE active) ---
    cmp dword [0x531C], 1           ; boot_info.fb_type == framebuffer?
    jne .skip_fb_map

    mov eax, [0x5304]               ; fb_addr physical address
    mov edx, eax
    shr edx, 30                     ; PDPT index (which 1GB region)

    test edx, edx
    jz .fb_in_low_1gb               ; PDPT[0] already -> PD at 0x3000

    ; Create PDPT entry -> PD at 0x4000 (already zeroed)
    push eax
    mov eax, edx
    shl eax, 3                      ; * 8
    add eax, 0x2000                 ; PDPT entry address
    mov dword [eax], 0x4003         ; Present | Writable | -> 0x4000
    mov dword [eax+4], 0
    pop eax

    ; Calculate PD index and write to PD at 0x4000
    shr eax, 21
    and eax, 0x1FF                  ; 9-bit PD index
    shl eax, 3                      ; * 8
    add eax, 0x4000                 ; PD entry address
    jmp .fb_write_pd

.fb_in_low_1gb:
    ; FB in low 1GB: use existing PD at 0x3000
    shr eax, 21
    and eax, 0x1FF
    shl eax, 3
    add eax, 0x3000

.fb_write_pd:
    ; Map 4MB (2 consecutive 2MB huge pages) for framebuffer
    mov edx, [0x5304]
    and edx, 0xFFE00000             ; align to 2MB boundary
    or edx, 0x83                    ; Present | Writable | PS (2MB page)
    mov [eax], edx
    mov dword [eax+4], 0
    add edx, 0x200000               ; next 2MB
    mov [eax+8], edx
    mov dword [eax+12], 0

.skip_fb_map:

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
    
    ; Verify that LME was actually set (check if in QEMU 32-bit)
    rdmsr
    bt eax, 8           ; Check LME bit
    jnc .longmode_not_supported

    mov si, longmode_setup_ok_msg
    call print_message
    ret

.longmode_not_supported:
    ; Long mode setup failed - fall back to 32-bit
    mov si, longmode_failed_msg
    call print_message
    jmp use_32bit_fallback

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
; Load x86 Kernel (sector 12, 1200 sectors → 0x10000-0xA5FFF)
; Chunked load: 9×128 sectors + 1×48 sectors = 1200 total
; NOTE: x86 kernel binary is ~1130 sectors; 1200 gives headroom for growth.
;       x64 kernel is placed at sector 2048 to avoid overlap.
; =======================================================================
load_kernel_86:
    mov si, loading_x86_msg
    call print_message
    ; Check for LBA extensions
    mov ah, 0x41
    mov bx, 0x55aa
    mov dl, 0x80
    int 0x13
    jc .chs_x86

    ; Load in chunks of 128 sectors (64KB per chunk, one segment each)
    mov word [lba_cur], 12     ; Starting LBA = sector 12
    mov word [seg_cur], 0x1000 ; Starting segment (physical 0x10000)
    mov word [chunks], 9       ; 9 full chunks of 128 sectors = 1152 sectors

.load_loop:
    mov si, disk_packet
    mov byte [si],   0x10
    mov byte [si+1], 0
    mov word [si+2], 128
    mov word [si+4], 0
    mov ax, [seg_cur]
    mov word [si+6], ax
    xor eax, eax
    mov ax, [lba_cur]
    mov dword [si+8],  eax
    mov dword [si+12], 0
    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc .x86_err

    add word [lba_cur], 128
    add word [seg_cur], 0x1000
    dec word [chunks]
    jnz .load_loop

    ; Final partial chunk: 48 sectors (9*128 + 48 = 1200 total)
    mov si, disk_packet
    mov byte [si],   0x10
    mov byte [si+1], 0
    mov word [si+2], 48
    mov word [si+4], 0
    mov ax, [seg_cur]
    mov word [si+6], ax
    xor eax, eax
    mov ax, [lba_cur]
    mov dword [si+8],  eax
    mov dword [si+12], 0
    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc .x86_err

    mov ax, 1
    ret

.chs_x86:
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 16
    mov ch, 0
    mov cl, 13                  ; CHS sector 13 = LBA 12
    mov dh, 0
    mov dl, 0x80
    int 0x13
    jc .x86_err
    mov ax, 1
    ret
.x86_err:
    xor ax, ax
    ret

; =======================================================================
; Enter 32-bit Protected Mode (x86 kernel path, NO paging)
; =======================================================================
enter_protected_mode_32:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1                 ; PE only (no PG — x86 kernel runs flat)
    mov cr0, eax
    jmp 0x18:protected_mode_32_start   ; CS = 0x18 = 32-bit code selector

; -----------------------------------------------------------------------
; 32-bit startup stub: copy kernel to 1 MB then jump to it
; -----------------------------------------------------------------------
[BITS 32]
protected_mode_32_start:
    mov ax, 0x20                ; 32-bit data selector
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000            ; temporary stack (kernel_main will relocate)
    ; Copy x86 kernel: 0x10000 → 0x100000 (1200 sectors = 614400 bytes)
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, (1200 * 512) / 4
    cld
    rep movsd
    jmp 0x18:0x100000           ; jump to x86 kernel entry point

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
    
    ; Copy kernel from 0x10000 to 0x100000 (1536 sectors for x64 kernel)
    ; 1536 sectors = 768KB (x64 kernel is ~561KB, with headroom for growth)
    mov rsi, 0x10000
    mov rdi, 0x100000
    mov rcx, (1536 * 512) / 8     ; 1536 sectors * 512 bytes / 8 bytes per qword
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

use_32bit_fallback:
    ; 32-bit fallback when long mode setup fails
    ; First, load the 32-bit kernel from sector 12
    mov si, loading_x86_msg
    call print_message
    
    ; Check for LBA extensions
    mov ah, 0x41
    mov bx, 0x55aa
    mov dl, 0x80
    int 0x13
    jc .use_chs_32
    
    ; Load x86 kernel in chunks (900 sectors total from sector 12 = 6*128 + 132)
    mov word [lba_cur_fb], 12
    mov word [seg_cur_fb], 0x1000
    mov word [chunks_fb], 6  ; 6 full chunks of 128 sectors
    
.load_32_loop:
    mov si, disk_packet_fb
    mov byte [si],   0x10
    mov byte [si+1], 0
    mov word [si+2], 128
    mov word [si+4], 0
    mov ax, [seg_cur_fb]
    mov word [si+6], ax
    xor eax, eax
    mov ax, [lba_cur_fb]
    mov dword [si+8],  eax
    mov dword [si+12], 0
    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc .use_chs_32
    
    add word [lba_cur_fb], 128
    add word [seg_cur_fb], 0x1000
    dec word [chunks_fb]
    jnz .load_32_loop
    
    ; Load final chunk (132 sectors) - 6*128 + 132 = 900
    mov si, disk_packet_fb
    mov byte [si],   0x10
    mov byte [si+1], 0
    mov word [si+2], 132
    mov word [si+4], 0
    mov ax, [seg_cur_fb]
    mov word [si+6], ax
    xor eax, eax
    mov ax, [lba_cur_fb]
    mov dword [si+8],  eax
    mov dword [si+12], 0
    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    
    jmp .enter_32bit_pmode

.use_chs_32:
    ; Simple CHS fallback - just load minimal  kernel
    jmp .enter_32bit_pmode

.enter_32bit_pmode:

.enter_32bit_pmode:
    cli
    
    ; Load GDT for protected mode
    lgdt [gdt_descriptor_32]
    
    ; Enter protected mode
    mov eax, cr0
    or al, 1            ; Set PE bit
    mov cr0, eax
    
    ; Far jump to protected mode
    jmp 0x08:protected_mode_32bit

protected_mode_32bit:
    [BITS 32]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    
    ; Kernel should be loaded at 0x10000, copy to 0x100000
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, 102400
    rep movsd
    
    ; Jump to kernel
    jmp 0x100000

    [BITS 16]
    
gdt_descriptor_32:
    dw gdt_end_32 - gdt_start_32 - 1
    dd gdt_start_32

gdt_start_32:
    ; Null descriptor
    dd 0x0
    dd 0x0
    ; Code segment
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10011010b
    db 11001111b
    db 0x0
    ; Data segment
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0
gdt_end_32:

; =======================================================================
; VBE Mode Setup — switch to VESA framebuffer mode
; Stores boot_info at 0x5300, VBE info at 0x5000/0x5200
; Falls back to text mode (fb_type=0) on any failure
; =======================================================================
vbe_setup:
    pusha
    push es

    ; Write default boot_info: text mode fallback
    mov dword [0x5300], 0x56424549  ; magic "VBEI"
    mov dword [0x5304], 0           ; fb_addr
    mov dword [0x5308], 0           ; fb_addr_high
    mov dword [0x530C], 0           ; fb_width
    mov dword [0x5310], 0           ; fb_height
    mov dword [0x5314], 0           ; fb_pitch
    mov dword [0x5318], 0           ; fb_bpp
    mov dword [0x531C], 0           ; fb_type = 0 (text mode)

    ; Query VBE Controller Info at 0x5000
    xor ax, ax
    mov es, ax
    mov di, 0x5000
    mov dword [es:di], 0x32454256   ; "VBE2" request signature
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done

    ; Try mode 0x118 (1024x768x32bpp)
    mov ax, 0x4F01
    mov cx, 0x0118
    mov di, 0x5200
    int 0x10
    cmp ax, 0x004F
    jne .try_mode_115
    ; Accept 32bpp or 24bpp
    cmp byte [0x5219], 32
    je .set_mode_118
    cmp byte [0x5219], 24
    jne .try_mode_115
.set_mode_118:
    ; Set mode 0x118 + LFB (bit 14)
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    je .vbe_store

.try_mode_115:
    ; Try mode 0x115 (800x600x32bpp)
    mov ax, 0x4F01
    mov cx, 0x0115
    mov di, 0x5200
    int 0x10
    cmp ax, 0x004F
    jne .try_mode_112
    cmp byte [0x5219], 32
    je .set_mode_115
    cmp byte [0x5219], 24
    jne .try_mode_112
.set_mode_115:
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    jne .try_mode_112
    jmp .vbe_store

.try_mode_112:
    ; Try mode 0x112 (640x480x32bpp)
    mov ax, 0x4F01
    mov cx, 0x0112
    mov di, 0x5200
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    cmp byte [0x5219], 32
    je .set_mode_112
    cmp byte [0x5219], 24
    jne .vbe_done
.set_mode_112:
    mov ax, 0x4F02
    mov bx, 0x4112
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done

.vbe_store:
    ; Populate boot_info from VBE Mode Info Block at 0x5200
    mov eax, [0x5228]               ; PhysBasePtr (offset 40)
    mov [0x5304], eax
    mov dword [0x5308], 0           ; high 32 bits = 0
    movzx eax, word [0x5212]        ; XResolution (offset 18)
    mov [0x530C], eax
    movzx eax, word [0x5214]        ; YResolution (offset 20)
    mov [0x5310], eax
    movzx eax, word [0x5210]        ; BytesPerScanLine (offset 16)
    mov [0x5314], eax
    movzx eax, byte [0x5219]        ; BitsPerPixel (offset 25)
    mov [0x5318], eax
    mov dword [0x531C], 1           ; fb_type = 1 (framebuffer)

.vbe_done:
    pop es
    popa
    ret

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
    dq 0x0000000000000000       ; 0x00  null
    dq 0x00209A0000000000       ; 0x08  64-bit code  (DPL=0, L=1)
    dq 0x0000920000000000       ; 0x10  64-bit data  (DPL=0)
    dq 0x00CF9A000000FFFF       ; 0x18  32-bit code  (DPL=0, 4 GB flat)
    dq 0x00CF92000000FFFF       ; 0x20  32-bit data  (DPL=0, 4 GB flat)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dq gdt_start

; =======================================================================
; Data Section
; =======================================================================
disk_packet: times 16 db 0

; Fallback variables for 32-bit kernel loading
disk_packet_fb: times 16 db 0
lba_cur_fb  dw 0
seg_cur_fb  dw 0
chunks_fb   dw 0

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
longmode_failed_msg    db "Long mode not available, using 32-bit...", 13, 10, 0
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
loading_x86_msg        db "Loading 32-bit x86 kernel...", 13, 10, 0