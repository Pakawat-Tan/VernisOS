;
;  Flow bootloader diagram Stage 2
;
; ====================================================================================
; |  Stage 2 Bootloader (Real Mode @ 0x8000)                                         |
; |                                                                                  |
; |  ┌── ตั้งค่า Segment Register, Stack และ Clear หน้าจอ                                |
; |  ├── อ่านค่า cpu_mode ที่ Stage 1 ส่งมาไว้ที่ [0x7FF0]                                   |
; |  │     ├── 1 → รองรับ Long Mode (x86_64)                                          |
; |  │     ├── 2 → รองรับ Protected Mode เท่านั้น (x86 32-bit)                           |
; |  │     └── อื่นๆ → Unsupported Architecture (อาจเป็น ARM)                           |
; |  ┌── ตรวจสอบ cpu_mode                                                            |
; |  ├── [1] → เตรียมโหมด 64-bit                                                      |
; |  │     ├── แสดงข้อความเตรียมเข้า 64-bit                                             |
; |  │     ├── โหลด Stage 3 จาก LBA sector 32 → 0x90000                              |
; |  │     │     ├── ใช้ BIOS LBA (INT 13h AH=42h) ถ้าไม่ได้ fallback เป็น CHS            |
; |  │     ├── เปิด A20 Line                                                          |
; |  │     └── Jump ไป Stage 3 ที่ 0x0000:0x9000                                       |
; |  ├── [2] → เตรียมโหมด Protected Mode (32-bit)                                     |
; |  │     ├── แสดงข้อความเตรียมเข้า 32-bit                                             |
; |  │     ├── โหลด Kernel จาก LBA sector 8 → 0x10000                                |
; |  │     │     ├── ใช้ BIOS LBA (INT 13h AH=42h) ถ้าไม่ได้ fallback เป็น CHS            |
; |  │     ├── เปิด A20 Line                                                          |
; |  │     └── Jump ไป setup_protected_mode                                          |
; |  │            ├── Load GDT                                                       |
; |  │            ├── เปิด CR0 PE-bit                                                 |
; |  │            └── Far jump เข้า protected_mode_entry                              |
; |  └── [อื่น ๆ] → แสดงข้อความ "Unsupported Architecture" และ Halt                     |
; |                                                                                  |
; |  หมายเหตุเพิ่มเติม:                                                                  |
; |    - ใช้ BIOS Function: INT 10h สำหรับข้อความ/หน้าจอ                                 |
; |    - ใช้ Fast A20 (Port 0x92) สำหรับเปิด A20 Line                                   |
; |    - ใช้ GDT ขนาดเล็กสำหรับ Protected Mode                                          |
; ====================================================================================
;
; =====================================================================================================================
; ตารางที่อยู่ (Address Table) สำหรับ Stage 2
; =====================================================================================================================
;| Address  | Bytes | Opcode           | Operator                      | Description
;-----------|-------|------------------|-------------------------------|-----------------------------------------------
;| 0x8000   | 1     | FA               | cli                           | Disable interrupts
;| 0x8001   | 2     | 8C C8            | mov ax, cs                    | Load CS to AX
;| 0x8003   | 2     | 8E D8            | mov ds, ax                    | Set DS = CS
;| 0x8005   | 2     | 8E C0            | mov es, ax                    | Set ES = CS
;| 0x8007   | 3     | B8 6C 00         | mov ax, 0x006C                | Load SS segment
;| 0x800A   | 2     | 8E D0            | mov ss, ax                    | Set SS
;| 0x800C   | 3     | BC 00 04         | mov sp, 0x0400                | Set SP (stack pointer)
;| 0x800F   | 1     | FB               | sti                           | Enable interrupts
;| 0x8010   | 3     | E8 xx xx         | call clear_screen             | Clear screen
;| 0x8013   | 3     | A0 F0 7F         | mov al, [0x7FF0]              | Read CPU mode byte
;| 0x8016   | 3     | A2 xx xx         | mov [cpu_mode], al            | Store CPU mode
;| 0x8019   | 3     | BE xx xx         | mov si, stage2_msg            | Load message pointer
;| 0x801C   | 3     | E8 xx xx         | call print_msg                | Print message
;| 0x801F   | 4     | 80 3E xx 01      | cmp byte [cpu_mode],1         | Compare cpu_mode == 1
;| 0x8023   | 2     | 74 xx            | je prepare_64bit              | Jump if 64-bit mode
;| 0x8025   | 4     | 80 3E xx 02      | cmp byte [cpu_mode],2         | Compare cpu_mode == 2
;| 0x8029   | 2     | 74 xx            | je prepare_32bit              | Jump if 32-bit mode
;| 0x802B   | 3     | BE xx xx         | mov si, unsupported_msg       | Unsupported arch message
;| 0x802E   | 3     | E8 xx xx         | call print_msg                | Print message
;| 0x8031   | 2     | EB xx            | jmp hang                      | Halt system
;| 0x8033   | 3     | BE xx xx         | mov si, mode32_msg            | 32-bit mode message
;| 0x8036   | 3     | E8 xx xx         | call print_msg                | Print message
;| 0x8039   | 3     | E8 xx xx         | call load_kernel32            | Load 32-bit kernel
;| 0x803C   | 3     | E8 xx xx         | call enable_a20               | Enable A20
;| 0x803F   | 2     | EB xx            | jmp setup_protected_mode      | Setup protected mode
;| 0x8041   | 3     | BE xx xx         | mov si, loading_kernel_msg    | Loading kernel message
;| 0x8044   | 3     | E8 xx xx         | call print_msg                | Print message
;| 0x8047   | 2     | B4 02            | mov ah, 0x02                  | BIOS read sectors function
;| 0x8049   | 2     | B0 04            | mov al, 4                     | Read 4 sectors
;| 0x804B   | 2     | BCh 00           | mov ch, 0                     | Cylinder 0
;| 0x804D   | 2     | B1 21            | mov cl, 6                     | Sector 6
;| 0x804F   | 2     | B2 00            | mov dh, 0                     | Head 0
;| 0x8051   | 2     | B3 80            | mov dl, 0x80                  | Drive 0x80
;| 0x8053   | 3     | BE 00 10         | mov si, 0x1000                | Load address ES:BX
;| 0x8056   | 2     | CD 13            | int 0x13                      | BIOS Disk Read
;| 0x8058   | 2     | 72 xx            | jc load_error                 | Jump on error
;| 0x805A   | 1     | C3               | ret                           | Return
;| 0x805B   | 1     | 60               | pusha                         | Save registers
;| 0x805C   | 3     | BE xx xx         | mov si, a20_msg               | Enable A20 message
;| 0x805F   | 3     | E8 xx xx         | call print_msg                | Print message
;| 0x8062   | 2     | EC               | in al, 0x92                   | Read port 0x92
;| 0x8063   | 2     | 84 C0            | test al, 2                    | Test bit 1
;| 0x8065   | 2     | 74 xx            | jz done                       | Jump if A20 already enabled
;| 0x8067   | 3     | 80 C8 02         | or al, 2                      | Set bit 1
;| 0x806A   | 3     | 80 E0 FE         | and al, 0xFE                  | Clear bit 0
;| 0x806D   | 2     | E6 92            | out 0x92, al                  | Write port 0x92
;| 0x806F   | 3     | B9 00 10         | mov cx, 0x1000                | Delay counter
;| 0x8072   | 2     | E2 FD            | loop delay                    | Delay loop
;| 0x8074   | 1     | 61               | popa                          | Restore registers
;| 0x8075   | 1     | C3               | ret                           | Return
;| 0x8076   | 3     | BE xx xx         | mov si, protected_msg         | Protected mode message
;| 0x8079   | 3     | E8 xx xx         | call print_msg                | Print message
;| 0x807C   | 1     | FA               | cli                           | Disable interrupts
;| 0x807D   | 7     | 0F 01 15 xx xx   | lgdt [gdt_descriptor]         | Load GDT descriptor
;| 0x8084   | 3     | 0F 20 C0         | mov eax, cr0                  | Read CR0
;| 0x8087   | 2     | 0C 01            | or al, 1                      | Set PE bit
;| 0x8089   | 3     | 0F 22 C0         | mov cr0, eax                  | Write CR0
;| 0x808C   | 5     | EA xx xx 08 00   | jmp 0x08:protected_mode_entry | Jump to protected mode
;| 0x8091   | 1     | FA               | cli                           | Disable interrupts (hang)
;| 0x8092   | 1     | F4               | hlt                           | Halt CPU
;| 0x8093   | 2     | EB FE            | jmp hang                      | Infinite loop
;|======================================================================================================================
; มายเหตุ: Address และ Bytes อาจเปลี่ยนได้ขึ้นอยู่กับการคอมไพล์จริง แต่โดยประมาณจะอยู่ช่วงนี้
;
; =====================================================================================================================
; Bootloader Stage 2: ตรวจสอบ cpu_mode และโหลด Stage 3 ตามสถาปัตยกรรม
; =====================================================================================================================
;
[BITS 16]
[ORG 0x8000]                ; เปลี่ยนเป็น 0x0000 เพราะจะถูกโหลดไปที่ 0x8000

start:
    ; Initialize segments สำหรับ address 0x8000
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax

    mov ax, 0x006C
    mov ss, ax
    mov sp, 0x0400
    sti

    ; Clear screen
    call clear_screen

    ; อ่านข้อมูล CPU mode จาก stage 1
    mov al, [0x7FF0]        ; อ่านจาก address ที่ stage 1 เก็บไว้
    mov [cpu_mode], al

    mov si, stage2_msg
    call print_msg

    ; ตรวจสอบ CPU mode ที่ได้จาก stage 1
    cmp byte [cpu_mode], 1
    je prepare_64bit
    cmp byte [cpu_mode], 2
    je prepare_32bit
    
    ; ถ้าไม่ใช่ x86 architecture
    mov si, unsupported_msg
    call print_msg
    jmp hang

prepare_64bit:
    mov si, mode64_msg
    call print_msg
    
    ; โหลด stage 3 สำหรับ 64-bit
    call load_stage3
    
    ; Enable A20
    call enable_a20
    
    ; Jump ไป stage 3
    jmp 0x0000:0x9000

prepare_32bit:
    mov si, mode32_msg
    call print_msg
    
    ; โหลด kernel 32-bit
    call load_kernel32
    
    ; Enable A20
    call enable_a20
    
    ; Setup protected mode
    jmp setup_protected_mode

; =====================================================
; Load Stage 3 (64-bit transition code)
; =====================================================
load_stage3:
    mov si, loading_stage3_msg
    call print_msg

    ; ใช้ BIOS extensions
    mov ah, 0x41
    mov bx, 0x55aa
    mov dl, 0x80
    int 0x13
    jc use_chs_stage3

    ; Use LBA
    mov si, disk_packet
    mov byte [si], 0x10      ; Packet size
    mov byte [si+1], 0       ; Reserved
    mov word [si+2], 4       ; Number of sectors
    mov word [si+4], 0       ; Offset
    mov word [si+6], 0x0900  ; Segment
    mov dword [si+8], 6     ; LBA sector 6
    mov dword [si+12], 0     ; Upper 32-bit LBA

    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc load_error
    ret

use_chs_stage3:
    ; Use CHS - sector 33 (1-based) = sector 32 (0-based) + 1
    mov ax, 0x0900
    mov es, ax
    xor bx, bx

    mov ah, 0x02
    mov al, 0x04           ; 4 sectors
    mov ch, 0x00           ; Cylinder 0
    mov cl, 0x07           ; Sector 7 (0x07 = 7 decimal)
    mov dh, 0x00           ; Head 0
    mov dl, 0x80
    int 0x13
    jc load_error
    ret

; =====================================================
; Load 32-bit Kernel
; =====================================================
load_kernel32:
    mov si, loading_kernel_msg
    call print_msg

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
    mov word [si+2], 13      ; 13 sectors for kernel
    mov word [si+4], 0
    mov word [si+6], 0x1000  ; Load to 0x10000
    mov dword [si+8], 12      ; LBA sector 12
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

    mov ah, 0x02
    mov al, 0x10           ; 16 sectors
    mov ch, 0x00
    mov cl, 0x0D           ; Sector 13
    mov dh, 0x00
    mov dl, 0x80
    int 0x13
    jc load_error
    ret

; =====================================================
; Enhanced A20 Enable
; =====================================================
enable_a20:
    pusha
    mov si, a20_msg
    call print_msg
    
    ; Method 1: Fast A20
    in al, 0x92
    test al, 2
    jnz .done
    or al, 2
    and al, 0xFE
    out 0x92, al
    
    ; Small delay
    mov cx, 0x1000
.delay:
    loop .delay
    
.done:
    popa
    ret

; =====================================================
; Setup Protected Mode (32-bit path)
; =====================================================
setup_protected_mode:
    mov si, protected_msg
    call print_msg
    
    cli
    lgdt [gdt_descriptor]
    
    mov eax, cr0
    or al, 1
    mov cr0, eax
    
    jmp 0x08:protected_mode_entry

; =====================================================
; Error Handlers
; =====================================================
load_error:
    mov si, load_error_msg
    call print_msg
    jmp hang

hang:
    cli
    hlt
    jmp hang

; =====================================================
; Print Message Function
; =====================================================
print_msg:
    pusha
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

; =====================================================
; Clear Screen Function
; =====================================================
clear_screen:
    pusha
    
    ; Set video mode to clear screen
    mov ax, 0x0003          ; 80x25 color text mode
    int 0x10
    
    ; Set cursor to top-left
    mov ah, 0x02            ; Set cursor position
    mov bh, 0x00            ; Page 0
    mov dh, 0x00            ; Row 0
    mov dl, 0x00            ; Column 0
    int 0x10
    
    ; Alternative method: Fill screen with spaces
    mov ah, 0x06            ; Scroll up function
    mov al, 0x00            ; Clear entire screen
    mov bh, 0x07            ; White on black attribute
    mov cx, 0x0000          ; Upper left corner (row 0, col 0)
    mov dx, 0x184F          ; Lower right corner (row 24, col 79)
    int 0x10
    
    ; Reset cursor position
    mov ah, 0x02
    mov bh, 0x00
    mov dh, 0x00
    mov dl, 0x00
    int 0x10
    
    popa
    ret

; =======================================================================
; Protected Mode Entry (32-bit)
; =======================================================================
[BITS 32]
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; Copy kernel to final location
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, 4096           ; 16 sectors * 512 bytes / 4
    rep movsd

    ; Jump to kernel
    jmp 0x100000

; =======================================================================
; Global Descriptor Table
; =======================================================================
[BITS 16]
align 8
gdt_start:
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

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; =======================================================================
; Data Section
; =======================================================================
align 4
disk_packet: times 16 db 0
cpu_mode db 0

; Messages
stage2_msg          db "Stage 2 bootloader started", 13, 10, 0
mode64_msg          db "Preparing 64-bit mode transition", 13, 10, 0
mode32_msg          db "Preparing 32-bit protected mode", 13, 10, 0
unsupported_msg     db "Unsupported architecture", 13, 10, 0
loading_stage3_msg  db "Loading Stage 3 (64-bit)...", 13, 10, 0
loading_kernel_msg  db "Loading 32-bit kernel...", 13, 10, 0
a20_msg             db "Enabling A20 line...", 13, 10, 0
protected_msg       db "Entering protected mode...", 13, 10, 0
load_error_msg      db "Error loading from disk!", 13, 10, 0