# Phase 17 — Prepare Developer Preview

> สัปดาห์ 46–48 | ภาษา: Makefile + Doc tools | สถานะ: — (อนาคต)

---

## เป้าหมาย

เตรียม **VernisOS Developer Preview v0.1.0** ให้พร้อมเผยแพร่ ครอบคลุมการ build release artifact, สร้าง distribution package, ตรวจสอบ release checklist ทุกข้อ, และจัดทำเอกสาร known limitations รวมถึง roadmap สำหรับ Phase 18+

---

## ภาพรวม

```
Release Pipeline:

  make prerequisites          ← ตรวจ toolchain ครบ
        │
        ▼
  make version                ← อัปเดต VERSION file
        │
        ▼
  make release                ← clean + full build + size-report
        │
        ├── make all ARCH=x86
        ├── make all ARCH=x64
        ├── make test-all         ← ต้อง PASS ทุก test
        └── make size-report      ← ต้องผ่าน size targets
        │
        ▼
  make dist                   ← สร้าง vernisOS-0.1.0.tar.gz
        │
        ▼
  vernisOS-0.1.0.tar.gz       ← release artifact
  ├── os.img          (4 MB disk image)
  ├── README.md
  ├── docs/
  ├── Makefile
  └── src/            (source code, ไม่มี build artifacts)
```

---

## ไฟล์ที่เกี่ยวข้อง

```
Makefile                        # เพิ่ม release, dist, prerequisites, version
VERSION                         # NEW: ไฟล์เก็บ version string "0.1.0"
CHANGELOG.md                    # NEW: บันทึกการเปลี่ยนแปลงทุก phase
docs/
├── README.md                   # Overview + quick start
├── KNOWN_ISSUES.md             # Known limitations v0.1.0
└── dev/
    └── phases/
        └── PHASE*.md           # Phase documentation ทุกไฟล์
```

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. make prerequisites — ตรวจ Toolchain

```makefile
# Makefile

REQUIRED_TOOLS = nasm gcc ld python3 qemu-system-x86_64 \
                 qemu-system-i386 rustc cargo objcopy

prerequisites:
	@echo "=== Checking prerequisites ==="
	@MISSING=0; \
	for tool in $(REQUIRED_TOOLS); do \
	    if command -v $$tool >/dev/null 2>&1; then \
	        echo "  [OK] $$tool ($(shell $$tool --version 2>&1 | head -1))"; \
	    else \
	        echo "  [MISSING] $$tool"; \
	        MISSING=1; \
	    fi; \
	done; \
	if [ $$MISSING -eq 1 ]; then \
	    echo "ERROR: กรุณาติดตั้ง tools ที่ขาด"; exit 1; \
	fi
	@echo "=== All prerequisites OK ==="
```

ตัวอย่าง output:

```
=== Checking prerequisites ===
  [OK] nasm (NASM version 2.16.01)
  [OK] gcc (gcc (Homebrew GCC 13.2.0))
  [OK] ld (GNU ld (GNU Binutils) 2.41)
  [OK] python3 (Python 3.12.2)
  [OK] qemu-system-x86_64 (QEMU emulator version 8.2.2)
  [OK] qemu-system-i386 (QEMU emulator version 8.2.2)
  [OK] rustc (rustc 1.76.0)
  [OK] cargo (cargo 1.76.0)
  [OK] objcopy (GNU objcopy (GNU Binutils) 2.41)
=== All prerequisites OK ===
```

---

### 2. VERSION File + make version

```makefile
# Makefile

VERSION_FILE = VERSION
VERSION      = $(shell cat $(VERSION_FILE) 2>/dev/null || echo "0.0.0")

version:
	@echo "VernisOS version: $(VERSION)"

version-set:
	@echo "$(NEW_VERSION)" > $(VERSION_FILE)
	@echo "Version set to: $(NEW_VERSION)"
	@sed -i 's/VERNISОС_VERSION "[^"]*"/VERNISОС_VERSION "$(NEW_VERSION)"/' \
	     include/version.h
```

```c
/* include/version.h */
#define VERNISОС_VERSION "0.1.0"
#define VERNISОС_CODENAME "Amber"
#define VERNISОС_BUILD_DATE __DATE__
```

CLI command `version` แสดงข้อมูล:

```
VernisOS> version
VernisOS v0.1.0 "Amber"
Build: 2026-04-04 (x86_64)
Kernel: Phase 17 Developer Preview
```

---

### 3. make release — Full Release Build

```makefile
# Makefile

release: prerequisites clean
	@echo "=== VernisOS Release Build: v$(VERSION) ==="
	$(MAKE) all ARCH=x86
	$(MAKE) all ARCH=x64
	@echo "=== Running test suite ==="
	$(MAKE) test-all
	@echo "=== Size report ==="
	$(MAKE) size-report
	@echo "=== Release build complete: v$(VERSION) ==="

clean:
	rm -rf build/
	find . -name "*.o" -delete
	find . -name "*.a" -delete
	cargo clean --manifest-path rust_module/Cargo.toml
```

```
make release output:

=== VernisOS Release Build: v0.1.0 ===
[clean] build artifacts removed
[build] x86 kernel... done (183 KB)
[build] x64 kernel... done (241 KB)
[build] Rust module... done (31 KB)
=== Running test suite ===
  selftest (x86) ... PASS
  selftest (x64) ... PASS
  integration (x86) ... PASS
  integration (x64) ... PASS
  [total] 56 tests, 0 failed
=== Size report ===
  x86 kernel: 183 KB  [< 200 KB ✓]
  x64 kernel: 241 KB  [< 250 KB ✓]
  Rust module: 31 KB
=== Release build complete: v0.1.0 ===
```

---

### 4. make dist — สร้าง Distribution Package

```makefile
# Makefile

DIST_NAME    = vernisOS-$(VERSION)
DIST_ARCHIVE = $(DIST_NAME).tar.gz

dist: release
	@echo "=== Creating distribution: $(DIST_ARCHIVE) ==="
	mkdir -p dist/$(DIST_NAME)
	# copy disk image (4 MB)
	cp build/os.img   dist/$(DIST_NAME)/
	# copy documentation
	cp README.md      dist/$(DIST_NAME)/
	cp -r docs/       dist/$(DIST_NAME)/docs/
	cp CHANGELOG.md   dist/$(DIST_NAME)/
	cp VERSION        dist/$(DIST_NAME)/
	# copy Makefile + source (no build artifacts)
	cp Makefile       dist/$(DIST_NAME)/
	rsync -a --exclude='build/' \
	         --exclude='*.o' --exclude='*.a' \
	         --exclude='target/' \
	         src/ dist/$(DIST_NAME)/src/
	# สร้าง archive
	cd dist && tar -czf $(DIST_ARCHIVE) $(DIST_NAME)/
	rm -rf dist/$(DIST_NAME)
	@echo "=== Created: dist/$(DIST_ARCHIVE) ==="
	@ls -lh dist/$(DIST_ARCHIVE)
```

```
vernisOS-0.1.0.tar.gz (โครงสร้างภายใน):

vernisOS-0.1.0/
├── os.img              ← 4 MB disk image (bootable)
├── README.md           ← Quick start guide
├── CHANGELOG.md        ← Release notes
├── VERSION             ← "0.1.0"
├── Makefile            ← สำหรับ rebuild จาก source
├── docs/
│   ├── KNOWN_ISSUES.md
│   └── dev/phases/PHASE*.md
└── src/
    ├── kernel/         ← Kernel source (C + ASM)
    ├── include/        ← Header files
    ├── ai/             ← Python AI bridge
    └── rust_module/    ← Rust no_std module
```

---

## โครงสร้างข้อมูล / API หลัก

### Release Checklist (ทุกข้อต้องผ่านก่อน tag release)

```
Release Checklist v0.1.0
══════════════════════════════════════════════════════

Tests:
  [ ] selftest_run_all() ผ่าน 0 failed (x86 + x64)
  [ ] test_cli_permission.py ผ่านทุก test
  [ ] test_integration.py ผ่านทุก scenario (x86 + x64)
  [ ] ผ่านทั้งหมด 56 unit tests

Binary Size:
  [ ] x86 kernel binary < 200 KB
  [ ] x64 kernel binary < 250 KB
  [ ] make size-report แสดง ✓ ทุกรายการ

Documentation:
  [ ] README.md อัปเดตล่าสุด
  [ ] CHANGELOG.md บันทึก Phase 11–17 ครบ
  [ ] KNOWN_ISSUES.md เขียนแล้ว
  [ ] PHASE14–17.md ครบ

Version:
  [ ] VERSION file = "0.1.0"
  [ ] include/version.h อัปเดตแล้ว
  [ ] `version` CLI command แสดง v0.1.0

Final:
  [ ] make dist สร้าง .tar.gz สำเร็จ
  [ ] .tar.gz extract + make + boot ใน QEMU ได้
  [ ] git tag v0.1.0
```

### Makefile Targets Summary

```makefile
# ── Development ──
make all             # build สำหรับ x64 (default)
make all ARCH=x86    # build สำหรับ x86
make clean           # ลบ build artifacts

# ── Testing ──
make test            # integration test x64
make test-x86        # integration test x86
make test-all        # test + test-x86

# ── Optimization ──
make size-report     # แสดงขนาด binary

# ── Release ──
make prerequisites   # ตรวจ toolchain
make version         # แสดง version
make release         # clean + build + test + size-report
make dist            # สร้าง .tar.gz distribution

# ── Info ──
make help            # แสดง targets ทั้งหมด
```

---

## ขั้นตอนการทำงาน

### Developer Preview Release Process

```
Phase 17 Timeline (สัปดาห์ 46–48):

  Week 46: Preparation
  ├── เขียน CHANGELOG.md (Phase 1–17 summary)
  ├── เขียน KNOWN_ISSUES.md
  ├── ตรวจ/อัปเดต README.md
  ├── ตั้ง VERSION = "0.1.0"
  └── make prerequisites → แก้ไข toolchain issues

  Week 47: Validation
  ├── make release → แก้ไข test failures
  ├── make size-report → optimize ถ้าเกิน limit
  ├── ทดสอบ make dist → extract → boot ใน QEMU จริง
  └── ให้คนอื่น peer review checklist

  Week 48: Release
  ├── make dist → สร้าง vernisOS-0.1.0.tar.gz
  ├── git tag v0.1.0
  └── เผยแพร่ Developer Preview
```

---

## Known Limitations — v0.1.0

| ข้อจำกัด | รายละเอียด | แผนแก้ไข |
|---------|-----------|---------|
| **No dynamic binary loading** | user programs รันเป็น kernel module เท่านั้น ไม่รองรับ ELF loader | Phase 19: ELF user-space loader |
| **No network stack** | ไม่มี TCP/IP, ไม่มี Ethernet driver | Phase 18: network stack (lwIP) |
| **No USB support** | ไม่รองรับ USB HID, USB storage | Phase 20: XHCI driver |
| **Single-core only** | ไม่รองรับ SMP / multi-core | Phase 21: SMP + per-core scheduler |
| **No swap / virtual memory** | physical memory เท่านั้น, ไม่มี paging | Phase 22: VMM + swap |
| **No filesystem persistence** | VernisFS อยู่ใน RAM เท่านั้น | Phase 18: disk-backed VernisFS |
| **AI bridge localhost only** | AI bridge ทำงานได้เฉพาะ localhost | Phase 18: remote AI bridge |

```
Memory Map v0.1.0 (x64):

  0x0000_0000 ─── IVT / BIOS data
  0x0000_7C00 ─── Bootloader (512 bytes)
  0x0001_0000 ─── Kernel code + data
  0x0010_0000 ─── Heap (4 MB)
  0x0050_0000 ─── Module area (2 MB)
  0x0070_0000 ─── Stack (256 KB)
  0x0074_0000 ─── VernisFS RAM disk (1 MB)
              ─── [available RAM]
```

---

## ผลลัพธ์

### สิ่งที่ได้จาก Phase 17

```
artifacts:
  dist/vernisOS-0.1.0.tar.gz     ← release package
  build/os_x86.img               ← bootable x86 disk image
  build/os_x64.img               ← bootable x64 disk image

documentation:
  CHANGELOG.md                   ← Phase 1–17 summary
  docs/KNOWN_ISSUES.md           ← v0.1.0 known limitations
  README.md                      ← updated quick start

version:
  VernisOS v0.1.0 "Amber"
  git tag: v0.1.0
```

### Feature Matrix v0.1.0

| Feature | x86 | x64 | หมายเหตุ |
|---------|-----|-----|---------|
| Bootloader (BIOS) | ✅ | ✅ | Assembly/NASM |
| Memory Manager | ✅ | ✅ | kmalloc/kfree |
| Scheduler | ✅ | ✅ | Round-robin + priority |
| IPC Channels | ✅ | ✅ | Ring buffer |
| VernisFS (RAM) | ✅ | ✅ | In-memory FS |
| CLI | ✅ | ✅ | Multi-user + roles |
| Permission System | ✅ | ✅ | root/admin/user |
| Rust Module | — | ✅ | no_std |
| Sandbox | ✅ | ✅ | Capability-based |
| AI Bridge | — | ✅ | Python side-car |
| klog | ✅ | ✅ | 256-entry ring buffer |
| Selftest | ✅ | ✅ | Boot-time checks |
| Audit Log | ✅ | ✅ | 64-entry ring buffer |

---

## สิ่งที่ต่อใน Phase ถัดไป

### Roadmap Phase 18+

```
Phase 18 — Network Stack
  ├── lwIP integration (bare-metal)
  ├── RTL8139 / virtio-net driver (QEMU)
  ├── TCP/IP stack (no socket syscall yet)
  └── AI bridge over TCP (แทน serial)

Phase 19 — ELF User-Space Loader
  ├── ELF32/ELF64 parser
  ├── User-space process isolation
  ├── System call interface (POSIX subset)
  └── Simple user-space libc (newlib)

Phase 20 — USB Support
  ├── XHCI host controller driver
  ├── USB HID (keyboard/mouse)
  └── USB Mass Storage (FAT32 read)

Phase 21 — Multi-Core (SMP)
  ├── APIC init + IPI
  ├── Per-core run queue
  ├── Spinlock + RCU primitives
  └── CPU affinity API

Phase 22 — Virtual Memory + Swap
  ├── Page table management (4-level PT x64)
  ├── Demand paging
  ├── Swap to disk
  └── mmap() syscall
```

```
Timeline คร่าวๆ:

  2026 Q2  ─── v0.1.0 Developer Preview (Phase 17) ← ตอนนี้
  2026 Q3  ─── v0.2.0 Network + ELF (Phase 18–19)
  2026 Q4  ─── v0.3.0 USB + SMP (Phase 20–21)
  2027 Q1  ─── v0.4.0 VMM + Swap (Phase 22)
  2027 Q2  ─── v1.0.0 First stable release
```
