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

    ; VBE framebuffer mode switch (real mode, before A20/protected mode)
    call vbe_setup

    ; Enable A20
    call enable_a20

    ; Setup protected mode (skip clear_screen - let kernel handle it)
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
    mov word [si+2], 6       ; Number of sectors (6, expanded for VBE code)
    mov word [si+4], 0       ; Offset
    mov word [si+6], 0x0900  ; Segment
    mov dword [si+8], 7     ; LBA sector 7 (moved for expanded stage2)
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
    mov al, 0x06           ; 6 sectors (expanded for VBE code)
    mov ch, 0x00           ; Cylinder 0
    mov cl, 0x08           ; Sector 8 (CHS 1-based = LBA 7)
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

    ; LBA read loop: max 127 sectors per call (SeaBIOS limit)
    ; Load 1200 sectors from LBA 13 → physical 0x10000 (kernel_x86.bin is ~565KB)
    mov word [lba_cur], 13
    mov word [seg_cur], 0x1000
    mov word [sects_left], 1200

.lba_loop:
    mov ax, [sects_left]
    test ax, ax
    jz .lba_done

    cmp ax, 127
    jle .lba_do
    mov ax, 127
.lba_do:
    mov si, disk_packet
    mov byte [si+0], 0x10
    mov byte [si+1], 0
    mov word [si+2], ax
    mov word [si+4], 0
    mov cx, [seg_cur]
    mov word [si+6], cx
    movzx ecx, word [lba_cur]
    mov dword [si+8], ecx
    mov dword [si+12], 0

    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc load_error

    mov ax, [disk_packet+2]    ; sectors just read
    sub [sects_left], ax
    add [lba_cur], ax
    shl ax, 5                  ; sectors * 32 = segment increment (512/16=32)
    add [seg_cur], ax
    jmp .lba_loop

.lba_done:
    ret

use_chs_kernel:
    ; CHS fallback: load 2 tracks (126 sectors = ~64KB) — partial load only
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    mov ah, 0x02
    mov al, 50             ; sectors 14-63 on track 0 head 0
    mov ch, 0x00
    mov cl, 0x0E
    mov dh, 0x00
    mov dl, 0x80
    int 0x13
    jc load_error

    mov ax, 0x1000
    mov es, ax
    mov bx, 0x6400         ; offset after 50 sectors (50*512=0x6400)

    mov ah, 0x02
    mov al, 63             ; track 0 head 1
    mov ch, 0x00
    mov cl, 0x01
    mov dh, 0x01
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
    
    ; Far jump to flush pipeline
    jmp 0x08:protected_mode_entry

; =====================================================
; Error Handlers
; =====================================================
load_error:
    mov si, load_error_msg
    call print_msg
    jmp hang

; =======================================================================
; VBE Mode Setup — switch to VESA framebuffer mode (x86 path)
; Stores boot_info at 0x5300
; =======================================================================
vbe_setup:
    pusha
    push es

    ; Default boot_info: text mode fallback
    mov dword [0x5300], 0x56424549  ; magic "VBEI"
    mov dword [0x5304], 0
    mov dword [0x5308], 0
    mov dword [0x530C], 0
    mov dword [0x5310], 0
    mov dword [0x5314], 0
    mov dword [0x5318], 0
    mov dword [0x531C], 0           ; fb_type = 0 (text mode)

    ; Query VBE Controller Info at 0x5000
    xor ax, ax
    mov es, ax
    mov di, 0x5000
    mov dword [es:di], 0x32454256   ; "VBE2"
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done

    ; Try mode 0x118 (1024x768x32/24bpp)
    mov ax, 0x4F01
    mov cx, 0x0118
    mov di, 0x5200
    int 0x10
    cmp ax, 0x004F
    jne .try_115
    cmp byte [0x5219], 32
    je .set_118
    cmp byte [0x5219], 24
    jne .try_115
.set_118:
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    je .vbe_store

.try_115:
    mov ax, 0x4F01
    mov cx, 0x0115
    mov di, 0x5200
    int 0x10
    cmp ax, 0x004F
    jne .try_112
    cmp byte [0x5219], 32
    je .set_115
    cmp byte [0x5219], 24
    jne .try_112
.set_115:
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    jne .try_112
    jmp .vbe_store

.try_112:
    mov ax, 0x4F01
    mov cx, 0x0112
    mov di, 0x5200
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    cmp byte [0x5219], 32
    je .set_112
    cmp byte [0x5219], 24
    jne .vbe_done
.set_112:
    mov ax, 0x4F02
    mov bx, 0x4112
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done

.vbe_store:
    mov eax, [0x5228]               ; PhysBasePtr
    mov [0x5304], eax
    mov dword [0x5308], 0
    movzx eax, word [0x5212]        ; XResolution
    mov [0x530C], eax
    movzx eax, word [0x5214]        ; YResolution
    mov [0x5310], eax
    movzx eax, word [0x5210]        ; BytesPerScanLine
    mov [0x5314], eax
    movzx eax, byte [0x5219]        ; BitsPerPixel
    mov [0x5318], eax
    mov dword [0x531C], 1           ; fb_type = 1 (framebuffer)

.vbe_done:
    pop es
    popa
    ret

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
    mov esp, 0x500000       ; Stack at 5MB (matches kernel _start expectation)

    ; Ensure DF is clear (forward direction)
    cld
    
    ; Use rep movsd (should be safe with DF clear)
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, 113664        ; 444KB in dwords
    rep movsd
    
    ; Ensure copy is complete before jumping
    ; Add volatile operations to prevent compiler reordering
    mov eax, dword [0x100000]  ; Read back first dword to verify copy
    mov eax, dword [0x16FFFC]  ; Read back last dword to verify copy

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
lba_cur   dw 0
seg_cur   dw 0
sects_left dw 0

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