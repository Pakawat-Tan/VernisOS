.section .text
.global _start

_start:
    // ตั้งค่า stack pointer
    ldr x0, =stack_top
    mov sp, x0
    
    // เคลียร์ BSS section
    ldr x0, =bss_start
    ldr x1, =bss_end
    mov x2, #0
clear_bss:
    cmp x0, x1
    b.ge clear_bss_done
    str x2, [x0], #8
    b clear_bss
clear_bss_done:

    // เรียก main function
    bl main
    
    // infinite loop หาก main return
halt:
    wfi
    b halt

// Main function
main:
    // แสดงข้อความ "Hello ARM64 Bootloader!"
    ldr x0, =hello_msg
    bl print_string
    
    // รอ input จาก user
    bl wait_for_input
    
    ret

// ฟังก์ชันแสดงข้อความ (UART output)
print_string:
    mov x1, x0              // เก็บ string address
    ldr x2, =0x09000000     // QEMU UART0 base address
    
print_loop:
    ldrb w0, [x1], #1       // โหลด character
    cbz w0, print_done      // หากเป็น null terminator ให้จบ
    str w0, [x2]            // เขียนไปยัง UART
    b print_loop
print_done:
    ret

// ฟังก์ชันรอ input
wait_for_input:
    ldr x1, =0x09000000     // UART base address
    add x1, x1, #0x18       // UART Flag Register offset
    
wait_loop:
    ldr w0, [x1]            // อ่าน flag register
    tbnz w0, #4, wait_loop  // ตรวจสอบ RXFE bit (bit 4)
    
    // อ่าน character
    ldr x1, =0x09000000     // UART base address
    ldr w0, [x1]            // อ่าน data register
    
    // echo character กลับ
    str w0, [x1]
    
    ret

.section .data
hello_msg:
    .ascii "Hello ARM64 Bootloader!\n"
    .ascii "Press any key to continue...\n"
    .byte 0

.section .bss
.align 8
stack_bottom:
    .space 4096
stack_top:

bss_start = .
bss_end = .