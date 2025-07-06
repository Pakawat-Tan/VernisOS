
### ตารางที่อยู่ (Address Table) CISC
| Address         | Bytes | Opcode                     | Operator                       | Description                                                                                 
|-----------------|-------|----------------------------|--------------------------------|---------------------------------------------------------------------------------------------
| 0x7C00          | 512   | -                          | -                              | จุดเริ่มต้นของ MBR (Master Boot Record) — BIOS โหลด sector แรกจาก disk มาที่ 0x7C00 (512 bytes)   
| 0x7C00          | 1     | `cli`                      | -                              | ปิด interrupt ทั้งหมด เพื่อป้องกันการขัดจังหวะระหว่างการทำงานของ bootloader                            
| 0x7C01          | 2     | `xor`                      | `ax, ax`                       | เคลียร์ค่าใน AX เพื่อเตรียมใช้ในการเซต segment registers                                  
| 0x7C03–0x7C0D   | 12    | `mov`                      | segment regs ← ax              | เซตค่า DS, ES, FS, GS, SS = 0 และ SP = 0x7C00                                            
| 0x7C10–0x7C21   | 18    | `pushfd/popfd`             | + logic                        | ตรวจสอบว่า CPU รองรับ CPUID instruction (โดย toggle bit 21 ของ EFLAGS)                    
| 0x7C23          | 3     | `mov si, msg_cpuid_ok`     | -                              | เตรียมข้อความแสดงว่า CPU รองรับ CPUID                                                     
| 0x7C26          | 2     | `call`                     | `print_message`                | เรียกฟังก์ชันแสดงข้อความผ่าน BIOS interrupt                                              
| 0x7C28–0x7C3E   | 23    | `cpuid/test`               | `eax/edx`                      | ตรวจสอบ Extended CPUID และ Long Mode support (EDX bit 29)                                 
| 0x7C40          | 4     | `mov`                      | `[cpu_mode], 1`                | เซตว่า CPU เป็น x86_64 (รองรับ long mode)                                                
| 0x7C44          | 3     | `jmp`                      | `load_stage2`                  | กระโดดไปโหลด Stage 2                                                                      
| 0x7C47–0x7C55   | 15    | `cpuid/jmp`                | (32-bit fallback)              | ตรวจสอบกรณีไม่รองรับ long mode → เซตเป็น x86 ธรรมดา                                   
| 0x7C60          | 3     | `mov`                      | `ax, 0x0800`                   | เตรียม ES = 0x0800 เพื่อโหลด Stage 2 ที่ address 0x8000                                    
| 0x7C63–0x7C6D   | 11    | `int 0x13`                 | `read sectors`                 | ใช้ BIOS interrupt 13h เพื่อโหลด Stage 2 จาก disk (sector 2–5 → 0x8000)                   
| 0x7C6E          | 3     | `mov si, msg_stage2_ok`    | -                              | แสดงข้อความโหลดสำเร็จ                                                                     
| 0x7C71          | 2     | `call`                     | `print_message`                | เรียกฟังก์ชันแสดงข้อความ                                                                 
| 0x7C73          | 3     | `mov`                      | `al ← cpu_mode`                | โหลดโหมด CPU ที่ตรวจสอบได้ไว้ที่ 0x7FF0                                                  
| 0x7C76          | 5     | `jmp`                      | `0x0000:0x8000`                | กระโดดไปยัง Stage 2 ที่โหลดไว้ที่ 0x8000                                                  
| 0x7C7B          | ...   | `no_cpuid:`                | Old Path                       | ถ้าไม่รองรับ CPUID ให้ถือว่าเป็น CPU old → แสดงข้อความ                    
| 0x7C90+         | ...   | `disk_error:`              | Error Handler                  | กรณีโหลด Stage 2 ไม่สำเร็จ → แสดงข้อความ “Disk read error!” และหยุดการทำงาน            
| 0x7D00+         | ...   | `print_message`            | ฟังก์ชัน BIOS Text Output         | ใช้ INT 0x10 (AH=0Eh) แสดงข้อความในโหมด TTY                                            
| 0x7E00+         | <32   | `cpu_mode db`              | ข้อมูล CPU mode                  | ค่า 0=Old, 1=x86_64, 2=x86                                                               
| 0x7E20+         | ~150  | `db`                       | ข้อความข้อความต่าง ๆ              | เช่น ‘CPUID supported’, ‘Stage 2 loaded’, ‘Disk read error!’                             
| 0x7FFE          | 2     | `dw 0xAA55`                | Boot Signature                 | MBR Signature ที่ BIOS ต้องการเพื่อบูต (อยู่ตำแหน่งท้ายสุดที่ offset 510–511)            
| 0x8000          | 1     | FA                         | cli                            | Disable interrupts                 
| 0x8001          | 2     | 8C C8                      | mov ax, cs                     | Load CS to AX                     
| 0x8003          | 2     | 8E D8                      | mov ds, ax                     | Set DS = CS                      
| 0x8005          | 2     | 8E C0                      | mov es, ax                     | Set ES = CS                      
| 0x8007          | 3     | B8 6C 00                   | mov ax, 0x006C                 | Load SS segment                  
| 0x800A          | 2     | 8E D0                      | mov ss, ax                     | Set SS                          
| 0x800C          | 3     | BC 00 04                   | mov sp, 0x0400                 | Set SP (stack pointer)           
| 0x800F          | 1     | FB                         | sti                            | Enable interrupts                
| 0x8010          | 3     | E8 xx xx                   | call clear_screen              | Clear screen                    
| 0x8013          | 3     | A0 F0 7F                   | mov al, [0x7FF0]               | Read CPU mode byte              
| 0x8016          | 3     | A2 xx xx                   | mov [cpu_mode], al             | Store CPU mode                  
| 0x8019          | 3     | BE xx xx                   | mov si, stage2_msg             | Load message pointer            
| 0x801C          | 3     | E8 xx xx                   | call print_msg                 | Print message                  
| 0x801F          | 4     | 80 3E xx 01                | cmp byte [cpu_mode],1          | Compare cpu_mode == 1           
| 0x8023          | 2     | 74 xx                      | je prepare_64bit               | Jump if 64-bit mode            
| 0x8025          | 4     | 80 3E xx 02                | cmp byte [cpu_mode],2          | Compare cpu_mode == 2           
| 0x8029          | 2     | 74 xx                      | je prepare_32bit               | Jump if 32-bit mode            
| 0x802B          | 3     | BE xx xx                   | mov si, unsupported_msg        | Unsupported arch message       
| 0x802E          | 3     | E8 xx xx                   | call print_msg                 | Print message                  
| 0x8031          | 2     | EB xx                      | jmp hang                       | Halt system                   
| 0x8033          | 3     | BE xx xx                   | mov si, mode32_msg             | 32-bit mode message           
| 0x8036          | 3     | E8 xx xx                   | call print_msg                 | Print message                  
| 0x8039          | 3     | E8 xx xx                   | call load_kernel32             | Load 32-bit kernel            
| 0x803C          | 3     | E8 xx xx                   | call enable_a20                | Enable A20                   
| 0x803F          | 2     | EB xx                      | jmp setup_protected_mode       | Setup protected mode          
| 0x8041          | 3     | BE xx xx                   | mov si, loading_kernel_msg     | Loading kernel message   
| 0x8044          | 3     | E8 xx xx                   | call print_msg                 | Print message                  
| 0x8047          | 2     | B4 02                      | mov ah, 0x02                   | BIOS read sectors function      
| 0x8049          | 2     | B0 04                      | mov al, 4                      | Read 4 sectors                 
| 0x804B          | 2     | BCh 00                     | mov ch, 0                      | Cylinder 0                    
| 0x804D          | 2     | B1 21                      | mov cl, 6                      | Sector 6                    
| 0x804F          | 2     | B2 00                      | mov dh, 0                      | Head 0                      
| 0x8051          | 2     | B3 80                      | mov dl, 0x80                   | Drive 0x80                  
| 0x8053          | 3     | BE 00 10                   | mov si, 0x1000                 | Load address ES:BX          
| 0x8056          | 2     | CD 13                      | int 0x13                       | BIOS Disk Read             
| 0x8058          | 2     | 72 xx                      | jc load_error                  | Jump on error              
| 0x805A          | 1     | C3                         | ret                            | Return                    
| 0x805B          | 1     | 60                         | pusha                          | Save registers            
| 0x805C          | 3     | BE xx xx                   | mov si, a20_msg                | Enable A20 message        
| 0x805F          | 3     | E8 xx xx                   | call print_msg                 | Print message            
| 0x8062          | 2     | EC                         | in al, 0x92                    | Read port 0x92           
| 0x8063          | 2     | 84 C0                      | test al, 2                     | Test bit 1               
| 0x8065          | 2     | 74 xx                      | jz done                        | Jump if A20 already enabled 
| 0x8067          | 3     | 80 C8 02                   | or al, 2                       | Set bit 1                
| 0x806A          | 3     | 80 E0 FE                   | and al, 0xFE                   | Clear bit 0              
| 0x806D          | 2     | E6 92                      | out 0x92, al                   | Write port 0x92          
| 0x806F          | 3     | B9 00 10                   | mov cx, 0x1000                 | Delay counter            
| 0x8072          | 2     | E2 FD                      | loop delay                     | Delay loop               
| 0x8074          | 1     | 61                         | popa                           | Restore registers        
| 0x8075          | 1     | C3                         | ret                            | Return                   
| 0x8076          | 3     | BE xx xx                   | mov si, protected_msg          | Protected mode message   
| 0x8079          | 3     | E8 xx xx                   | call print_msg                 | Print message            
| 0x807C          | 1     | FA                         | cli                            | Disable interrupts       
| 0x807D          | 7     | 0F 01 15 xx xx             | lgdt [gdt_descriptor]          | Load GDT descriptor      
| 0x8084          | 3     | 0F 20 C0                   | mov eax, cr0                   | Read CR0                 
| 0x8087          | 2     | 0C 01                      | or al, 1                       | Set PE bit               
| 0x8089          | 3     | 0F 22 C0                   | mov cr0, eax                   | Write CR0                
| 0x808C          | 5     | EA xx xx 08 00             | jmp 0x08:protected_mode_entry  | Jump to protected mode   
| 0x8091          | 1     | FA                         | cli                            | Disable interrupts (hang)
| 0x8092          | 1     | F4                         | hlt                            | Halt CPU                 
| 0x8093          | 2     | EB FE                      | jmp hang                       | Infinite loop            
| 0x90000         | -     | -                          | -                              | จุดเริ่มต้นของ Stage 3 ที่โหลดโดย Stage 2 
| 0x90000         | 1     | `cli`                      | -                              | ปิด interrupt            
| 0x90001         | 2     | `xor`                      | `ax, ax`                       | เคลียร์ AX              
| 0x90003–0x9000D | ~11   | `mov`                      | เซต DS, ES, SS, SP             | เตรียม environment เริ่มต้น 
| 0x9000E         | 5     | `call print_message`       | `si = stage3_msg`              | แสดงข้อความเริ่มต้น      
| 0x90013         | ...   | `call check_a20`           | -                              | ตรวจสอบว่า A20 เปิดหรือไม่ 
| 0x90020         | ...   | `call enable_a20`          | -                              | หากยังไม่เปิด → พยายามเปิด 
| 0x90030         | ...   | `load_kernel_64:`          | -                              | ตรวจสอบว่า BIOS รองรับ INT 13h Extensions หรือไม่ 
| 0x90035–...     | ...   | `int 0x13`                 | EXT or CHS read sectors        | โหลด kernel จาก disk      
| 0x90080         | ...   | `setup_long_mode:`         | -                              | สร้าง page tables ที่ 0x1000–0x3000 
| 0x90090         | ...   | `mov cr4, ...`             | เปิด PAE                        | เปิด CR4.PAE             
| 0x900A0         | ...   | `wrmsr`                    | MSR_EFER ← LME                 | เปิด Long Mode (MSR 0xC0000080, bit 8) 
| 0x900B0         | ...   | `lgdt [gdt_descriptor]`    | -                              | โหลด GDT ใหม่            
| 0x900C0         | 5     | `jmp 0x08:long_mode_start` | Far Jump                       | กระโดดไปยัง long_mode_start (64-bit mode) 
| 0x90100         | -     | `long_mode_start:`         | -                              | จุดเริ่มต้น 64-bit code  
| 0x90100+        | ...   | `mov rax, 0x10000`         | src (kernel temp)              | เตรียม pointer เพื่อ copy kernel 
| 0x90110+        | ...   | `mov rdi, 0x100000`        | dst (kernel final)             | ชี้ไปยังตำแหน่งปลายทาง   
| 0x90120+        | ...   | `rep movsq`                | copy                           | คัดลอก kernel ไปยังตำแหน่ง 1MB 
| 0x90140         | ...   | `jmp 0x100000`             | jump to kernel entry           | กระโดดไปยัง Kernel       
| 0x90200+        | ~256  | `db`                       | ข้อความสำหรับ BIOS print         | เช่น "Stage 3", "A20 Error", "Disk read error!" 
