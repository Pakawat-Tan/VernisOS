# VernisOS — ระบบ Build

> ไฟล์ที่เกี่ยวข้อง: `Makefile`

---

## ภาพรวม

VernisOS ใช้ GNU Makefile เป็น build system สร้าง `os.img` เดียวที่รองรับทั้ง x86 และ x86_64

---

## Prerequisites (สิ่งที่ต้องติดตั้งก่อน)

```bash
# ตรวจสอบ toolchain
make prerequisites
```

| Tool | เพื่อ | ติดตั้ง (macOS) |
|------|------|----------------|
| `i686-elf-gcc` | Compile x86 kernel | `brew install i686-elf-gcc` |
| `x86_64-elf-gcc` | Compile x64 kernel | `brew install x86_64-elf-gcc` |
| `i686-elf-ld` | Link x86 kernel | (มาพร้อม i686-elf-binutils) |
| `x86_64-elf-ld` | Link x64 kernel | (มาพร้อม x86_64-elf-binutils) |
| `nasm` | Assemble boot stages + ISR stubs | `brew install nasm` |
| `cargo +nightly` | Build Rust no_std | `rustup install nightly` |
| `python3` | Policy compile + tests | `brew install python3` |
| `qemu-system-x86_64` | Run VMs | `brew install qemu` |

### Rust Targets

```bash
rustup target add x86_64-unknown-none
rustup component add rust-src --toolchain nightly
```

---

## Build Targets

### หลัก

| Command | ผล |
|---------|-----|
| `make` หรือ `make all` | Build ทุกส่วน + สร้าง `os.img` |
| `make build-x86` | Build แค่ x86 kernel binary |
| `make build-x64` | Build แค่ x64 kernel binary |
| `make build-core` | Build Rust static libraries |
| `make clean` | ลบ build artifacts ทั้งหมด |

### Rust Libraries

| Command | ผล |
|---------|-----|
| `make rust` | Force rebuild Rust libs ทั้ง x86 + x64 |
| `make rust32` | Force rebuild Rust lib (x86) เท่านั้น |
| `make rust64` | Force rebuild Rust lib (x64) เท่านั้น |

> ใช้ `make rust` เมื่อแก้ไขไฟล์ใน `kernel/core/verniskernel/src/`

### รัน VernisOS

| Command | ผล |
|---------|-----|
| `make run32` | รัน x86 image ใน QEMU (serial → stdout) |
| `make run64` | รัน x64 image ใน QEMU (serial → stdout) |
| `make run64-ai` | รัน x64 + COM2 bridge → TCP 4444 |

### Debug ด้วย GDB

| Command | ผล |
|---------|-----|
| `make debug32` | QEMU x86 หยุดรอ GDB ที่ port 1234 |
| `make debug64` | QEMU x64 หยุดรอ GDB ที่ port 1234 |

```bash
# Terminal 1
make debug64

# Terminal 2
x86_64-elf-gdb make/kernel/arch/x86_64/kernel_x64.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

### Test และ Release

| Command | ผล |
|---------|-----|
| `make test` | รัน integration tests (x64) |
| `make test-x86` | รัน integration tests (x86) |
| `make test-all` | รัน tests ทั้ง x86 + x64 |
| `make release` | Clean + Full build + Size report |
| `make dist` | สร้าง release archive (.tar.gz) |
| `make size-report` | แสดงขนาด binary แต่ละส่วน |
| `make version` | แสดง version string |

---

## Compiler Flags

### Shared Flags (ทั้ง x86 และ x64)

```makefile
CFLAGS = -Wall -Wextra -Os -std=gnu99 \
         -ffreestanding -fno-pie \
         -ffunction-sections -fdata-sections
```

| Flag | ความหมาย |
|------|----------|
| `-Os` | Optimize for size |
| `-ffreestanding` | ไม่มี standard library, ไม่มี main() |
| `-fno-pie` | ปิด Position-Independent Executable (kernel อยู่ fixed address) |
| `-ffunction-sections` | แต่ละ function ใน section ของตัวเอง → linker GC ได้ |
| `-fdata-sections` | แต่ละ global data ใน section ของตัวเอง |

### x86-specific

```makefile
CFLAGS_X86 = $(CFLAGS) -m32 -march=i686 -Wa,--noexecstack
```

| Flag | ความหมาย |
|------|----------|
| `-m32 -march=i686` | สร้าง 32-bit code สำหรับ i686 |
| `-Wa,--noexecstack` | mark stack เป็น non-executable |

### x64-specific

```makefile
CFLAGS_X64 = $(CFLAGS) -mno-red-zone -mcmodel=kernel -mgeneral-regs-only
```

| Flag | ความหมาย |
|------|----------|
| `-mno-red-zone` | ปิด red zone (128 bytes ใต้ RSP) — interrupt อาจทับได้ |
| `-mcmodel=kernel` | Kernel code model สำหรับ address สูง |
| `-mgeneral-regs-only` | ห้าม SSE/AVX ใน C code (interrupt handler ไม่ต้อง save XMM regs) |

### Rust Flags

```bash
# x86
cargo +nightly build -Zbuild-std=core,alloc \
    -Zjson-target-spec --target i386.json --release

# x64
RUSTFLAGS="-C no-redzone=yes" \
    cargo +nightly build -Zbuild-std=core,alloc \
    --target x86_64-unknown-none --release
```

---

## Linker

```makefile
# x86
i686-elf-ld -T kernel/arch/x86/linker.ld -m elf_i386 \
    --gc-sections [objects] libvernisos.a -o kernel_x86.elf

# x64
x86_64-elf-ld -T kernel/arch/x86_64/linker.ld -m elf_x86_64 \
    --gc-sections [objects] libvernisos_x64.a -o kernel_x64.elf
```

`--gc-sections` ลบ function/data ที่ไม่ถูกใช้งานเพื่อลดขนาด binary

---

## Output Files

| ไฟล์ | คำอธิบาย |
|------|----------|
| `make/boot/x86/stage1.bin` | MBR (512 bytes) |
| `make/boot/x86/stage2.bin` | Stage 2 (2048 bytes) |
| `make/boot/x86/stage3.bin` | Stage 3 (2048 bytes) |
| `make/kernel/arch/x86/kernel_x86.elf` | x86 kernel ELF (สำหรับ debug) |
| `make/kernel/arch/x86/kernel_x86.bin` | x86 kernel flat binary |
| `make/kernel/arch/x86_64/kernel_x64.elf` | x64 kernel ELF |
| `make/kernel/arch/x86_64/kernel_x64.bin` | x64 kernel flat binary |
| `make/policy.bin` | Policy blob (VPOL) |
| `make/vernisfs.bin` | VernisFS disk image |
| `lib/x86/libvernisos.a` | Rust staticlib (x86) |
| `lib/x86_64/libvernisos_x64.a` | Rust staticlib (x64) |
| `os.img` | Final disk image (4 MB) |

---

## ตัวอย่าง Build สมบูรณ์

```bash
# ครั้งแรก (Rust ยังไม่ build)
make rust        # Build Rust libraries (~2-5 นาที)
make             # Build ทุกอย่าง

# รัน
make run64       # x64 ใน QEMU

# แก้ไข code แล้ว rebuild เฉพาะส่วน
make             # make ตรวจ dependency เอง

# ถ้าแก้ Rust source
make rust64      # force rebuild Rust x64
make             # rebuild ที่เหลือ

# ดูขนาด
make size-report
```

---

## Troubleshooting

### `undefined reference to scheduler_set_quantum`
Rust library ยังไม่ได้ build:
```bash
make rust64
make
```

### Kernel ไม่ Boot (triple fault / reset loop)
ตรวจขนาด x64 binary — ต้องไม่เกิน ~460 KB:
```bash
wc -c make/kernel/arch/x86_64/kernel_x64.bin
```
ถ้าใหญ่เกิน → เพิ่ม sector count ใน `boot/x86/stage3.asm`

### Keyboard พิมพ์ไม่ได้ / Infinite loop
ตรวจว่า `ai_poll_cmd()` ไม่ได้ถูกเรียกใน readline idle loop
COM2 floating bus จะคืนค่า `0xFF` ทำให้ `ai_getc` วนไม่หยุด
แก้: ย้าย polling ไปไว้ใน IRQ0 timer handler

### QEMU ไม่มี display
```bash
qemu-system-x86_64 -drive format=raw,file=os.img -serial stdio -nographic
```
