;
;  Flow bootloader diagram 
;
; ===================================================================================
; |  ตรวจสอบว่า CPU รองรับ CPUID instruction หรือไม่                                    |
; |  ├── [Yes] → เรียกใช้งาน CPUID function                                           |
; |  │     ├── ตรวจสอบ CPU Architecture (32-bit หรือ 64-bit)                         |
; |  │     │     ├── [Long Mode Available] → โหลดและเริ่ม Kernel 64-bit (x86_64)      |
; |  │     │     └── [No Long Mode]        → โหลดและเริ่ม Kernel 32-bit (x86)         |
; |  │     └── (อาจเพิ่มการตรวจสอบ feature เพิ่มเติม เช่น virtualization, SSE, etc.)      |
; |  └── [No] → สมมุติเป็น non-x86 arch → เรียกโหลด ARM kernel (aarch32 หรือ aarch64)    |
; ===================================================================================
;
; ==============================================================================================================================================================================
; ตารางที่อยู่ (Address Table) สำหรับ Stage 1
; ==============================================================================================================================================================================
; Address         | Bytes | Opcode                  | Operator                 | Description                                                                                   |
;-----------------|-------|-------------------------|--------------------------|-----------------------------------------------------------------------------------------------|
; 0x7C00          | 512   | -                       | -                        | จุดเริ่มต้นของ MBR (Master Boot Record) — BIOS โหลด sector แรกจาก disk มาที่ 0x7C00 (512 bytes)     |
; 0x7C00          | 1     | `cli`                   | -                        | ปิด interrupt ทั้งหมด เพื่อป้องกันการขัดจังหวะระหว่างการทำงานของ bootloader                             |
; 0x7C01          | 2     | `xor`                   | `ax, ax`                 | เคลียร์ค่าใน AX เพื่อเตรียมใช้ในการเซต segment registers                                             |
; 0x7C03–0x7C0D   | 12    | `mov`                   | segment regs ← ax        | เซตค่า DS, ES, FS, GS, SS = 0 และ SP = 0x7C00                                                  |
; 0x7C10–0x7C21   | 18    | `pushfd/popfd`          | + logic                  | ตรวจสอบว่า CPU รองรับ CPUID instruction (โดย toggle bit 21 ของ EFLAGS)                          |
; 0x7C23          | 3     | `mov si, msg_cpuid_ok`  | -                        | เตรียมข้อความแสดงว่า CPU รองรับ CPUID                                                             |
; 0x7C26          | 2     | `call`                  | `print_message`          | เรียกฟังก์ชันแสดงข้อความผ่าน BIOS interrupt                                                         |
; 0x7C28–0x7C3E   | 23    | `cpuid/test`            | `eax/edx`                | ตรวจสอบ Extended CPUID และ Long Mode support (EDX bit 29)                                     |
; 0x7C40          | 4     | `mov`                   | `[cpu_mode], 1`          | เซตว่า CPU เป็น x86_64 (รองรับ long mode)                                                        |
; 0x7C44          | 3     | `jmp`                   | `load_stage2`            | กระโดดไปโหลด Stage 2                                                                          |
; 0x7C47–0x7C55   | 15    | `cpuid/jmp`             | (32-bit fallback)        | ตรวจสอบกรณีไม่รองรับ long mode → เซตเป็น x86 ธรรมดา                                               |
; 0x7C60          | 3     | `mov`                   | `ax, 0x0800`             | เตรียม ES = 0x0800 เพื่อโหลด Stage 2 ที่ address 0x8000                                            |
; 0x7C63–0x7C6D   | 11    | `int 0x13`              | `read sectors`           | ใช้ BIOS interrupt 13h เพื่อโหลด Stage 2 จาก disk (sector 2–5 → 0x8000)                          |
; 0x7C6E          | 3     | `mov si, msg_stage2_ok` | -                        | แสดงข้อความโหลดสำเร็จ                                                                           |
; 0x7C71          | 2     | `call`                  | `print_message`          | เรียกฟังก์ชันแสดงข้อความ                                                                           |
; 0x7C73          | 3     | `mov`                   | `al ← cpu_mode`          | โหลดโหมด CPU ที่ตรวจสอบได้ไว้ที่ 0x7FF0                                                             |
; 0x7C76          | 5     | `jmp`                   | `0x0000:0x8000`          | กระโดดไปยัง Stage 2 ที่โหลดไว้ที่ 0x8000                                                            |
; 0x7C7B          | ...   | `no_cpuid:`             | ARM Path                 | ถ้าไม่รองรับ CPUID ให้ถือว่าเป็น ARM → แสดงข้อความ และหยุดการทำงาน                                     |
; 0x7C90+         | ...   | `disk_error:`           | Error Handler            | กรณีโหลด Stage 2 ไม่สำเร็จ → แสดงข้อความ “Disk read error!” และหยุดการทำงาน                        |
; 0x7D00+         | ...   | `print_message`         | ฟังก์ชัน BIOS Text Output   | ใช้ INT 0x10 (AH=0Eh) แสดงข้อความในโหมด TTY                                                     |
; 0x7E00+         | <32   | `cpu_mode db`           | ข้อมูล CPU mode            | ค่า 0=ARM, 1=x86_64, 2=x86                                                                     |
; 0x7E20+         | ~150  | `db`                    | ข้อความข้อความต่าง ๆ        | เช่น ‘CPUID supported’, ‘Stage 2 loaded’, ‘Disk read error!’                                   |
; 0x7FFE          | 2     | `dw 0xAA55`             | Boot Signature           | MBR Signature ที่ BIOS ต้องการเพื่อบูต (อยู่ตำแหน่งท้ายสุดที่ offset 510–511)                              |
; =============================================================================================================================================================================
;
; =======================================================================
; Bootloader Stage 1: ตรวจสอบ CPUID และโหลด Stage 2 ตามสถาปัตยกรรม
; =======================================================================

bits 16                                                                                         ; กำหนดให้โค้ดทำงานในโหมด 16-bit
org 0x7C00                                                                                      ; เริ่มต้นที่ address 0x7C00 ซึ่งเป็นที่อยู่ของ MBR

_start:
    cli                                                                                         ; ปิด interrupt ทั้งหมด (Clear interrupt flag)
    xor ax, ax                                                                                  ; ล้างค่า AX (ตั้งค่าเป็น 0)

    ; Clear segment registers
    mov ds, ax                                                                                  ; ตั้งค่า DS (Data Segment) เป็น 0
    mov es, ax                                                                                  ; ตั้งค่า ES (Extra Segment) เป็น 0
    mov fs, ax                                                                                  ; ตั้งค่า FS (FS Segment) เป็น 0
    mov gs, ax                                                                                  ; ตั้งค่า GS (GS Segment) เป็น 0
    mov ss, ax                                                                                  ; ตั้งค่า SS (Stack Segment) เป็น 0
    mov sp, 0x7C00                                                                              ; ตั้งค่า stack pointer (SP) เริ่มต้นที่ 0x7C00

; =====================================================
; ตรวจสอบ CPUID Support (EFLAGS bit 21)
; =====================================================
    pushfd                          ; เก็บ EFLAGS (32-bit)
    pop eax                         ; โหลด EFLAGS ลงใน EAX
    mov ecx, eax                    ; เก็บค่าเดิมไว้ใน ECX
    xor eax, 1 << 21                ; Toggle CPUID bit (bit 21)
    push eax                        ; เก็บค่าที่แก้ไขลงบน stack
    popfd                           ; เขียนกลับลงใน EFLAGS
    pushfd                          ; อ่าน EFLAGS ใหม่
    pop eax                         ; ดึงค่าใหม่ลงใน EAX
    push ecx                        ; เก็บค่าเดิม
    popfd                           ; คืนค่า EFLAGS เดิม
    xor eax, ecx                    ; เปรียบเทียบ
    jz no_cpuid                     ; ถ้าไม่เปลี่ยน = ไม่รองรับ CPUID

    ; CPUID รองรับ - ตรวจสอบ Long Mode
    mov si, msg_cpuid_ok
    call print_message

    ; ตรวจสอบ Extended CPUID
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb no_long_mode

    ; ตรวจสอบ Long Mode support
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz no_long_mode

    mov byte [cpu_mode], 1
    jmp load_stage2

no_long_mode:
    ; ไม่รองรับ Long Mode
    mov si, msg_32bit
    call print_message
    mov byte [cpu_mode], 2
    jmp load_stage2

; =====================================================
; โหลด Stage 2 จาก sector 2–5 ไปยัง 0x8000
; =====================================================
load_stage2:
    mov ax, 0x0800          ; ES = 0x0800 (0x8000 >> 4)
    mov es, ax              ; ES:BX จะชี้ไปที่ 0x8000
    xor bx, bx              ; BX = 0x0000

    mov ah, 0x02            ; BIOS read sector function
    mov al, 0x04            ; จำนวน sectors (4)
    mov ch, 0x00            ; Cylinder 0
    mov cl, 0x02            ; Sector 2
    mov dh, 0x00            ; Head 0
    mov dl, 0x80            ; Drive (0x80 = first hard disk)
    int 0x13                ; BIOS disk service
    jc disk_error           ; ถ้ามี error (CF=1)
    
    ; ตรวจสอบว่าโหลดสำเร็จ
    mov si, msg_stage2_ok
    call print_message

    mov al, [cpu_mode]
    mov [0x7FF0], al

    jmp 0x0000:0x8000

; =====================================================
; กระโดดไปยัง ARM loader
; =====================================================
no_cpuid:
    ; ไม่รองรับ CPUID - สมมุติว่าเป็น ARM
    mov si, msg_arm
    call print_message
    mov byte [cpu_mode], 0
    ; สำหรับ ARM ให้หยุดไว้ก่อน
    jmp $

; =====================================================
; Massage
; =====================================================
disk_error:
    mov si, msg_disk_error                                                                      ; ถ้ามีข้อผิดพลาดในการอ่านดิสก์ให้แสดงข้อความ "Disk read error!"
    mov bx, 80                                                                                  ; เตรียมค่าสำหรับคำนวณ offset
    mov cx, 33                                                                                  ; ความยาวข้อความ
    mov dx, 12                                                                                  ; แสดงที่แถวที่ 12
    call print_message                                                                          ; เรียกฟังก์ชัน print_message เพื่อแสดงข้อความ
    jmp $                                                                                       ; ทำให้การทำงานหยุดที่นี่ (ให้ติดอยู่ในลูปถาวร)


; =====================================================
; ฟังก์ชันแสดงข้อความ
; Input: SI = pointer to null-terminated string
; =====================================================
print_message:
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
; Data
; =====================================================
cpu_mode db 0               ; 0=ARM, 1=x86, 2=x86_64

; =====================================================
; Messages
; =====================================================
msg_boot            db 'Universal Bootloader v1.0', 13, 10, 0
msg_cpuid_ok        db 'CPUID supported', 13, 10, 0
msg_long_mode       db '64-bit CPU detected', 13, 10, 0
msg_32bit           db '32-bit CPU detected', 13, 10, 0
msg_arm             db 'ARM CPU detected (CPUID not supported)', 13, 10, 0
msg_loading_stage2  db 'Loading Stage 2...', 13, 10, 0
msg_stage2_ok       db 'Stage 2 loaded successfully', 13, 10, 0
msg_disk_error      db 'Disk read error!', 13, 10, 0

; =====================================================
; Padding และ Signature
; =====================================================
times 510-($-$$) db 0                                                                           ; ทำการเติม 0 ให้ครบ 510 bytes
dw 0xAA55                                                                                       ; เพิ่ม signature MBR (0xAA55) เพื่อบ่งบอกว่าเป็น Master Boot Record