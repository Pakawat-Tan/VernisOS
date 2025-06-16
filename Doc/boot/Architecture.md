## 🧠 CPU Architecture Detection & Boot Flow (Stage-based Design)

### 🔍 Logic Diagram

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        Stage 1 Bootloader (Real Mode @ 0x7C00)                         │
├────────────────────────────────────────────────────────────────────────────────────────┤
 - ตรวจสอบว่า CPU รองรับ CPUID instruction หรือไม่                                          
   ├── [Yes] → เรียกใช้งาน CPUID function                                                 
   │     ├── ตรวจสอบ CPU Architecture (32-bit หรือ 64-bit)                               
   │     │     ├── [Long Mode Available] → โหลดและเริ่ม Stage 3 (x86_64)                  
   │     │     └── [No Long Mode] → โหลดและเริ่ม Kernel 32-bit (x86)                      
   │     └── (สามารถเพิ่มการตรวจสอบ feature อื่น ๆ เช่น virtualization, SSE, etc.)            
   └── [No] → สมมุติว่าเป็น non-x86 → โหลด ARM Kernel (AArch32 หรือ AArch64)                  

┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        Stage 2 Bootloader (Real Mode @ 0x8000)                         │
├────────────────────────────────────────────────────────────────────────────────────────┤
 - ตั้งค่า Segment Register, Stack และ Clear หน้าจอ                                   
 - อ่านค่า cpu_mode ที่ Stage 1 ส่งมาไว้ที่ [0x7FF0]                                   
   ├── 1 → รองรับ Long Mode (x86_64)                                                    
   ├── 2 → รองรับ Protected Mode เท่านั้น (x86 32-bit)                                 
   └── อื่นๆ → Unsupported Architecture (อาจเป็น ARM)                                  
                                                                                        
 - ตรวจสอบ cpu_mode                                                                    
   ├── [1] เตรียมโหมด 64-bit                                                            
   │   ├── แสดงข้อความเตรียมเข้า 64-bit                                                
   │   ├── โหลด Stage 3 จาก LBA sector 32 → 0x90000                                     
   │   ├── เปิด A20 Line                                                                 
   │   └── Jump ไป Stage 3 ที่ 0x0000:0x9000                                             
   ├── [2] เตรียมโหมด Protected Mode (32-bit)                                          
   │   ├── แสดงข้อความเตรียมเข้า 32-bit                                                
   │   ├── โหลด Kernel จาก LBA sector 8 → 0x10000                                       
   │   ├── เปิด A20 Line                                                                 
   │   └── Jump ไป setup_protected_mode                                                 
   │         ├── Load GDT                                                               
   │         ├── เปิด CR0 PE-bit                                                        
   │         └── Far jump เข้า protected_mode_entry                                     
   └── อื่น ๆ → แสดงข้อความ "Unsupported Architecture" และ Halt                      
                                                                                        
 💡 หมายเหตุ:                                                                            
   - ใช้ BIOS INT 10h สำหรับข้อความ                                                    
   - ใช้ Fast A20 (Port 0x92) เพื่อเปิด A20 Line                                        
   - ใช้ GDT ขนาดเล็กสำหรับ Protected Mode                                             


┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        Stage 3 Bootloader (Real Mode @ 0x9000)                         │
├────────────────────────────────────────────────────────────────────────────────────────┤
 - เคลียร์ Segment Registers และ Stack Pointer                                          
 - แสดงข้อความ "Stage 3 - 64-bit Kernel Loader" ผ่าน BIOS INT 10h                     
 - ตรวจสอบว่า A20 line เปิดใช้งานหรือยัง                                               
   ├── [Enabled] → ดำเนินการต่อ                                                        
   └── [Disabled] → พยายามเปิด A20, หากล้มเหลว → แสดง "A20 Error" และ halt          
                                                                                        
 - โหลด kernel โดยใช้ BIOS LBA หรือ fallback เป็น CHS                                  
   ├── [LBA Available] → โหลด kernel_x64 จาก sector 6 เป็นต้นไป                        
   └── [CHS Fallback] → โหลด kernel จากตำแหน่ง CHS ที่กำหนด                            
       ├── [Success] → ไปต่อ                                                            
       └── [Fail] → แสดง “Disk read error!” และ halt                                  
                                                                                        
 - เตรียม Memory Paging สำหรับ Long Mode                                               
   ├── Clear page table memory ที่ 0x1000, 0x2000, 0x3000                                
   ├── สร้าง PML4, PDPT, PD สำหรับ map 0x00000000–0x00200000 ด้วย 2MB page             
   ├── เปิด PAE (CR4.PAE = 1)                                                           
   └── เปิด Long Mode ผ่าน MSR EFER (bit LME)                                          
                                                                                        
 - สร้าง GDT ใหม่, เปิด Protected Mode + Paging (CR0 |= PG|PE)                          
 - Far Jump ไปยัง long_mode_start เพื่อเข้าสู่ 64-bit mode                              
                                                                                        
 [64-bit Mode]                                                                          
   ├── Set segment registers (DS, SS = 0)                                               
   ├── เคลียร์หน้าจอ, แสดง “Entering 64-bit mode...”                                    
   ├── Copy kernel (โหลดไว้ที่ 0x10000) ไปยัง 0x100000                                   
   └── Jump ไปยัง entry point ที่ 0x100000                                              

```
