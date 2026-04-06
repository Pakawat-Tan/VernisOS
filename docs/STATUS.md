# VernisOS — สถานะและแผนพัฒนา

> อัปเดตล่าสุด: 2026-04-04 | Version: 0.15.0

## สิ่งที่มีแล้ว

| ส่วน | รายละเอียด | Phase |
|------|-----------|-------|
| Bootloader 3-stage | Real Mode → Protected Mode → Long Mode, A20, GDT | 1–2 |
| Dual-arch kernel | x86 (i686) + x86_64 build จาก Makefile เดียว | 3 |
| GDT / IDT / PIC | Interrupt Descriptor Table, 8259 PIC remap | 3 |
| PIT Timer | 240 Hz, IRQ0, drive scheduler + AI polling + GUI rendering | 3, 24 |
| VGA Text Mode | 80×25, color output, scroll | 3 |
| Serial (COM1) | Debug output ผ่าน 0x3F8 | 4 |
| Keyboard Driver | PS/2 scancode → ASCII, IRQ1 | 4 |
| Scheduler (Rust) | Round-robin, preemptive via timer tick, PCB, priority, nice | 5 |
| Paging | Identity map 128MB + FB, frame allocator, 4KB/2MB mapping API | 16 |
| Heap Allocator (Rust) | Buddy system allocator (`buddy_system_allocator`) | 5 |
| IPC | Message-passing mailbox, 16 slots | 6 |
| Sandbox | Capability-based per-process permissions | 6 |
| CLI Shell | ~30 built-in commands (`help`, `ps`, `whoami`, `ls`, `cat`, `ping`, `lspci`, `kill`, …) | 7, 22-24 |
| AI Bridge (COM2) | Kernel ↔ Python via 0x2F8, REQ/RESP/EVT/CMD protocol | 8 |
| Python AI Listener | Anomaly detection, auto-tuner, behavior monitor | 9 |
| In-Kernel AI (Rust) | Event store, anomaly detector, auto-tuner, policy engine | 10 |
| AI Auto-Tuning | Real-time CMD polling via IRQ0, scheduler quantum tuning | 11 |
| Policy System | Binary VPOL format, access control rules, disk-persistent | 12 |
| VernisFS | ATA PIO, sector-based, 32 files, read/write/append/mkdir/rm | 13 |
| User Database | SHA-256 password hashing, login/logout, privilege levels | 13 |
| Security | Policy enforcement, audit log, kernel log (klog) | 13 |
| Self-Test | Boot-time validation of subsystems | 14 |
| Module Registry | Dynamic kernel module register/unregister | 6 |
| Framebuffer GUI | 1024×768×24-bit double buffering, Rust windowed GUI layer | 24 |
| Network Stack | E1000 driver, ARP+IPv4+ICMP, PCI enumeration, user commands | 22 |
| Process Signals | PCB signal_pending, signal delivery, POSIX priority | 23 |

---

## สิ่งที่ยังขาด

### วิกฤต — จำเป็นต้องมีเพื่อเป็น OS ที่สมบูรณ์

| # | Feature | รายละเอียด | ความยาก |
|---|---------|-----------|---------|
| 1 | **Paging / Virtual Memory** | ✅ Identity-mapped (x64: 4-level + 2MB pages, x86: PSE 4MB pages) 128MB + framebuffer — มี frame allocator + `paging_map_4k`/`paging_create_address_space` + user page mapping (PAGE_USER) ทำงานแล้ว | สูง |
| 2 | **User Space (Ring 3)** | ✅ Ring 3 user task ทำงานจริงแล้วทั้ง x86 + x64 — TSS (esp0/rsp[0]) + int 0x80 DPL=3 gate + user code/stack page mapping + iret/iretq privilege transition — user heartbeat test verified | สูง |
| 3 | **Context Switch** | ✅ Per-task kernel stack + round-robin timer preemption ทำงานจริงแล้ว (ทั้ง x86 + x86_64) — worker task วิ่ง concurrent กับ CLI ผ่าน stack switching ใน `isr_common_stub` | สูง |
| 4 | **ELF Loader** | ✅ ELF32/ELF64 parser + loader ทำงานแล้ว — โหลดจาก VernisFS, parse PT_LOAD segments, map เข้า user address space, สร้าง Ring 3 task ที่ ELF entry point — CLI `exec` command พร้อมใช้งาน | กลาง |
| 5 | **Exception Handling** | มี exception dispatch + diagnostics (#PF/#GP/#DF) และจำแนก user fault เพื่อ mark/kill process แล้ว + Phase 18 context switch ทำให้สามารถ resume ระบบได้หลัง kill user process | กลาง |

### สำคัญ — ทำให้ใช้งานได้จริงมากขึ้น

| # | Feature | รายละเอียด | ความยาก |
|---|---------|-----------|---------|
| 6 | **PCI Bus Enumeration** | ✅ ทำงานแล้ว — สแกนผ่าน PCI config space 0xCF8/0xCFC และแสดงผลผ่าน `lspci` | กลาง |
| 7 | **Network Stack** | ✅ ทำงานแล้ว (minimal) — E1000 driver + ARP + IPv4 + ICMP (`ping`) | สูง |
| 8 | **RTC / Wall Clock** | ✅ ทำงานแล้ว — อ่าน CMOS RTC (0x70/0x71) + คำสั่ง `date`/`uptime` | ต่ำ |
| 9 | **Process Lifecycle** | ✅ ทำงานแล้ว — มี `SYS_EXIT` / `SYS_WAITPID` / `SYS_GETPID` และทดสอบด้วย `/bin/exit32, /bin/exit64` | สูง |
| 10 | **Signals** | ✅ ทำงานแล้ว — PCB signal_pending bitmask, signal_send/get, SYS_KILL syscall (63), `kill` CLI command | กลาง |
| 11 | **Proper `read()`/`write()` Syscalls** | ✅ ทำงานแล้ว — เพิ่ม SYS_READ(64)/SYS_WRITE(65) แบบ path-based เชื่อม VernisFS พร้อม kernel-side user buffer copy (x86+x64) | กลาง |

### เพิ่มความสมบูรณ์ — ดีถ้ามี

| # | Feature | รายละเอียด | ความยาก |
|---|---------|-----------|---------|
| 12 | **VFS Abstraction Layer** | ✅ เริ่มใช้งานแล้ว — เพิ่ม `kfs_*` abstraction layer ครอบ VernisFS และย้าย syscall/CLI/userdb/ELF loader ไปใช้ผ่านชั้นกลาง | กลาง |
| 13 | **AHCI / NVMe Driver** | ✅ AHCI+NVMe เชื่อม KFS — auto-detect priority: NVMe > AHCI > ATA PIO, pluggable disk I/O | สูง |
| 14 | **USB Stack** | ⬜ ไม่มี UHCI/OHCI/EHCI/xHCI — ต่อ USB device ไม่ได้ → Phase 57 | สูงมาก |
| 15 | **Framebuffer Graphics / GUI** | ✅ ทำงานแล้ว — 1024×768×24bpp double-buffered GUI ที่ 240fps, cursor-only fast path, dirty rect tracking | กลาง |
| 16 | **ACPI / Power Management** | ✅ ทำงานแล้ว — เพิ่ม ACPI-lite driver (RSDP/RSDT/FADT/DSDT `_S5_`) สำหรับ `shutdown`/`restart` พร้อม reset-register และ QEMU fallback | กลาง |
| 17 | **Pipes / Unix Sockets** | ✅ ทำงานแล้ว — shell pipeline `|` + local Unix-socket layer (`ipc_usock_*`) บน IPC channels พร้อม CLI (`usockbind/usocksend/usockrecv/usockclose`) | กลาง |
| 18 | **`/dev` / `/proc` Pseudo-FS** | ✅ ทำงานแล้ว — เพิ่ม pseudo-files ใน `kfs_*` layer: `/proc/uptime`, `/proc/ps`, `/proc/fs`, `/dev/null`, `/dev/zero` พร้อม `ls`/`cat` ผ่าน CLI | ต่ำ |
| 19 | **Shared Libraries** | ✅ ทำงานแล้วระดับพื้นฐาน — เพิ่ม minimal dynamic loader (`dylib`) บน KFS+module loader พร้อม CLI (`dlopen/dlsym/dlcall/dlclose/dllist`) สำหรับโหลด/resolve/call symbols แบบ local | สูง |
| 20 | **SMP (Multi-core)** | ⬜ วิ่งบน single core เท่านั้น — ไม่มี AP startup, APIC → Phase 55-56 | สูงมาก |

---

## แผนพัฒนาแนะนำ

```
Phase 16: Paging  ✅ DONE
  └─ Identity map 128MB + framebuffer (x64: 2MB pages, x86: PSE 4MB)
  └─ Page fault handler (diagnostics + user-fault kill + task switch)
  └─ Frame allocator + `paging_map_4k` / `paging_create_address_space` API ready

Phase 17: Ring 3 + TSS  ✅ DONE
  └─ x64: TSS rsp[0] update on context switch, user 4K page mapping with PAGE_USER
  └─ x86: TSS32 struct + GDT slot 5 (0x28) + ltr, esp0 update on context switch
  └─ Both: user code page + stack mapped outside 128MB identity region
  └─ iret/iretq transition to Ring 3 (CS=0x1B, SS=0x23) — int 0x80 heartbeat verified

Phase 18: Context Switch  ✅ DONE
  └─ Save/restore register state ผ่าน timer IRQ
  └─ Stack switching (kernel stack per process)
  └─ Preemptive multitasking ที่ทำงานจริง

Phase 19: ELF Loader  ✅ DONE
  └─ ELF64 parser (x64) + ELF32 parser (x86) — validates magic, class, machine
  └─ PT_LOAD segment mapping: allocate frames, copy file data, map with PAGE_USER
  └─ elf_exec() / elf_exec_32(): read from VernisFS → create Ring 3 task at e_entry
  └─ /bin/hello64 + /bin/hello32 test ELFs in VernisFS image (mkfs_vernis.py)
  └─ CLI `exec <path>` command via g_elf_exec_fn function pointer

Phase 20: Process Lifecycle ✅ DONE
  └─ SYS_EXIT (60): terminate current process, deactivate task slot, scheduler_terminate_current(), context switch to next task
  └─ SYS_WAITPID (61): non-blocking — returns exit code if terminated, -1 if still running
  └─ SYS_GETPID (62): return current PID from Rust scheduler
  └─ Rust FFI: scheduler_get_exit_code() — retrieve terminated process's exit code
  └─ /bin/exit64 + /bin/exit32 test ELFs: heartbeat x5 → SYS_EXIT(42)
  └─ Both x86 + x64 tested: processes exit cleanly, system continues running

Phase 21: RTC + Uptime ✅ DONE
  └─ CMOS RTC read (ports 0x70/0x71): BCD→binary conversion, 12/24h mode, update-in-progress wait
  └─ `date` command: shows YYYY-MM-DD HH:MM:SS UTC from hardware RTC
  └─ `uptime` command: shows days/hours/minutes/seconds from kernel_tick / TIMER_HZ
  └─ Boot-time RTC print: [phase21] RTC: 2026-4-4 12:27:38 UTC
  └─ Both x86 + x64 tested: correct wall-clock time from QEMU CMOS

Phase 22: PCI + Network ✅ DONE
  └─ PCI bus enumeration implemented (0xCF8/0xCFC config space): scan 256 buses × 32 slots, export list to CLI
  └─ E1000 (Intel 82540EM) driver implemented for QEMU: BAR0 MMIO mapping, RX/TX descriptor rings, polling send/receive
  └─ Minimal L2/L3 stack implemented: Ethernet + ARP + IPv4 + ICMP echo (ping path)
  └─ CLI commands added: `lspci` (list devices), `ping <ip> [count]` (ICMP echo)
  └─ Boot-time network init/logs: detect NIC, print MAC, assign static IP 10.0.2.15
  └─ Both x86 + x64 tested in QEMU with `-device e1000,netdev=net0 -netdev user,id=net0`

Phase 23: Signals ✅ DONE
  └─ PCB signal_pending bitmask (one bit per signal 0-63)
  └─ Signal delivery via signal_send & get_pending_signal in Rust scheduler
  └─ FFI exports scheduler_signal_send / scheduler_get_pending_signal for C kernel
  └─ SYS_KILL syscall (63): kill(pid, sig) dispatches signals to target process
  └─ Signal priority: SIGKILL(9) > SIGTERM(15) > SIGINT(2) > others (POSIX ordering)
  └─ CLI command `kill <pid> <sig>`: send signal from userland
  └─ Both x86 + x64 tested: Phase-specific asm inline for syscall dispatch

Phase 24: GUI 240fps+ Performance ✅ DONE
  └─ Updated GUI_TARGET_FPS from 120→240 to match kernel PIT@240Hz timer frequency
  └─ Cursor-only fast path: restore underlay + draw new cursor + present partial rect (240fps guaranteed)
  └─ **Layer 1 - Dirty rect tracking (MMIO optimization)**:
    └─ Only dirty rect copied to framebuffer MMIO (not full 2.4MB screen)
    └─ Terminal output: 2.4MB MMIO write reduced to ~100-500B MMIO write (5,000-24,000× faster)
  └─ **Layer 2 - Per-row terminal rendering**:
    └─ dirty_rows[40] array tracks which terminal rows changed
    └─ terminal_render() skips clean rows entirely (renders only ~80 chars vs 3,200)
    └─ Single character output: 40× faster rendering
  └─ **Layer 3 - Batch terminal rendering (CRITICAL)**:
    └─ Problem: wm_window_draw_char() did linear window search 80 times per row (O(80n))
    └─ Solution: wm_render_rows_direct() renders 80 chars with 1 window lookup (O(n))
    └─ Result: 80× fewer window manager searches (80n → n complexity)
  └─ Frame pacing: cursor-only path bypasses throttle; full compose only on events/terminal changes
  └─ Build status: Rust ✅, x64 kernel ✅, x86 kernel ✅, os.img (4.0M) ✅
  └─ **Layer 4 - Frame timing throttling + cursor position caching (April 4, 20:51)**:
    └─ **Critical fix**: GUI main loop was running at full CPU speed instead of throttling to 240Hz
    └─ Problem: Loop processed events immediately without checking frame timing → desynchronized updates
    └─ Solution: Added frame interval check (ticks_since_last < frame_interval → skip frame)
    └─ Added LAST_CURSOR_X/Y cache to skip redundant renders when cursor position hasn't changed
    └─ Effect: Cursor movement now synchronized with PIT timer for smooth 240Hz visualization
    └─ Build status: Rust ✅, x64 kernel ✅, x86 kernel ✅, os.img (4.0M, 20:51) ✅
  └─ Performance result: Smooth 240fps cursor + mouse movement, minimal MMIO traffic, 80× faster terminal rendering, server-grade CPU efficiency

Phase 25: Proper read/write Syscalls to VernisFS ✅ DONE
  └─ Added SYS_READ(64) and SYS_WRITE(65) in x86 + x64 kernel paths
  └─ Implemented path-based file I/O syscall ABI:
    └─ read(path_ptr, user_buf_ptr, max_len) -> bytes read
    └─ write(path_ptr, user_buf_ptr, len) -> bytes written
  └─ Added kernel-side user memory range validation for Ring 3 address window (0x10000000-0x11000000)
  └─ Added safe path copy (bounded 64 bytes, NUL-terminated required)
  └─ Added user<->kernel buffer copy via temporary kernel buffer (max 4096 bytes)
  └─ Routed both int 0x80 and SYSCALL C dispatcher to same VFS handlers
  └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:00) ✅

Phase 26: VFS Abstraction Layer (Foundation) ✅ DONE
  └─ Added new kernel FS abstraction interface: `include/vfs.h` + `kernel/fs/vfs.c`
  └─ Mounted backend model introduced (`KFS_BACKEND_VERNISFS` for current backend)
  └─ Added unified APIs: `kfs_init`, `kfs_read_file`, `kfs_write_file`, `kfs_list_dir`, etc.
  └─ Migrated integration points from direct VernisFS calls to abstraction layer:
    └─ Kernel boot init (`kfs_init`) on x86 + x64
    └─ SYS_READ/SYS_WRITE handlers on x86 + x64
    └─ ELF loader read path on x86 + x64
    └─ CLI filesystem commands (`ls/cat/write/append/rm/mkdir`)
    └─ User DB load path (`/etc/shadow`)
  └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:04) ✅

Phase 27: ACPI / Power Management ✅ DONE
  └─ Added new ACPI-lite driver: `include/acpi.h` + `kernel/drivers/acpi.c`
  └─ Implements BIOS memory scan for RSDP, parses RSDT/FADT/DSDT
  └─ Extracts ACPI S5 sleep type from DSDT (`_S5_`) for real soft power-off
  └─ Uses FADT reset register when available for reboot path
  └─ Keeps legacy/QEMU fallbacks: ports 0x604/0x4004/0xB004 for shutdown, 0xCF9 + KBC reset for reboot
  └─ Wired into both x86 + x64 kernel init and existing CLI `shutdown` / `restart` commands
  └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:15) ✅

Phase 28: Shell Pipeline Support ✅ DONE
  └─ Added CLI output capture buffer and piped input buffer in `kernel/shell/cli.c`
  └─ Implemented recursive `cmd1 | cmd2` execution for built-in commands
  └─ Added pipe consumers:
    └─ `cat` can read from pipe
    └─ `write <path>` can write piped content
    └─ `append <path>` can append piped content
    └─ Added `grep <pattern> [path]`
    └─ Added `wc [path]`
  └─ Protected interactive/system commands from invalid piping (`ps`, `shutdown`, `restart`, `exec`, etc.)
  └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:27) ✅

  Phase 29: `/dev` + `/proc` Pseudo-FS ✅ DONE
    └─ Extended `kernel/fs/vfs.c` (`kfs_*`) with pseudo node support and synthetic `VfsFileEntry` nodes
    └─ Added pseudo directories and files:
      └─ `/proc`, `/proc/uptime`, `/proc/ps`, `/proc/fs`
      └─ `/dev`, `/dev/null`, `/dev/zero`
    └─ Dynamic `/proc` content:
      └─ `/proc/uptime` from kernel ticks + timer Hz
      └─ `/proc/ps` from scheduler snapshot (`scheduler_get_pid_list` + `scheduler_get_ps_row`)
      └─ `/proc/fs` backend/ready/file-count summary
    └─ Device semantics:
      └─ `/dev/null`: reads EOF, writes/append succeed and discard
      └─ `/dev/zero`: reads zero-filled bytes, writes/append discard
    └─ Protected pseudo paths from mkdir/rm/write to read-only proc nodes
    └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:35) ✅

  Phase 30: Unix-Socket Layer on IPC ✅ DONE
    └─ Extended IPC API in `include/ipc.h` with `ipc_usock_bind/connect/send/recv/close`
    └─ Implemented path registry in `kernel/ipc/ipc.c` (`IPC_MAX_USOCKETS`, path->channel mapping)
    └─ Local socket data path uses existing channel ring buffers (stream-like)
    └─ Added CLI commands in `kernel/shell/cli.c`:
      └─ `usockbind <path> [owner_pid]`
      └─ `usocksend <path> <data...>` (supports pipeline input)
      └─ `usockrecv <path> [max_bytes]`
      └─ `usockclose <path>`
    └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:40) ✅

  Phase 31: Minimal Shared-Library Loader ✅ DONE
    └─ Added `include/dylib.h` + `kernel/module/dylib.c`
    └─ Implemented in-kernel dynamic library slots with persistent storage pool
    └─ `dylib_open(path,name)` loads binary from KFS and delegates execution mapping to `module_load`
    └─ `dylib_resolve(handle,symbol)` resolves symbols in form `fnN` (mapped to module export index)
    └─ `dylib_call(handle,symbol,arg)` invokes resolved export through module call path
    └─ Added CLI commands in `kernel/shell/cli.c`:
      └─ `dlopen <path> [name]`
      └─ `dlsym <handle> <symbol>`
      └─ `dlcall <handle> <symbol> [arg]`
      └─ `dlclose <handle>`
      └─ `dllist`
    └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:42) ✅

    Phase 32: AHCI Foundation (Probe + Visibility) ✅ DONE
      └─ Added AHCI detection on both x86 + x64 from PCI class 01:06 (SATA AHCI)
      └─ Implemented controller bootstrap skeleton:
        └─ Enable PCI bus master + memory space
        └─ Read/map ABAR (BAR5) into kernel address space
        └─ Enable AHCI mode (GHC.AE)
        └─ Read PI (implemented ports) + VS (AHCI version)
      └─ Exported kernel status for shell diagnostics:
        └─ `kernel_ahci_available()`, `kernel_ahci_ports()`, `kernel_ahci_pi()`, `kernel_ahci_version()`
      └─ Added CLI command: `ahci` (show online state, AHCI version, PI bitmap, port count)
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:46) ✅

    Phase 33: AHCI Port Diagnostics ✅ DONE
      └─ Added per-port AHCI register export API for shell diagnostics:
        └─ `kernel_ahci_port_info(port, ssts, sig, cmd, tfd, isr)` (x86 + x64)
      └─ Added link/device classification via AHCI signatures:
        └─ SATA (0x00000101), SATAPI (0xEB140101), SEMB, PM, no-link
      └─ Boot serial now prints per-implemented-port summary (`[phase32] AHCI pN ...`)
      └─ CLI upgraded:
        └─ `ahci` shows controller status/version/PI
        └─ `ahci ports` shows per-port SSTS/SIG/CMD/TFD table
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 21:54) ✅

    Phase 34: AHCI Identify Command Path ✅ DONE
      └─ Added AHCI command-engine primitives (x86 + x64):
        └─ Port start/stop, command slot allocation, port rebase (CLB/FB/CTBA)
        └─ Command list + command table + PRDT + RFIS static buffers per port/slot
      └─ Implemented ATA IDENTIFY DEVICE (0xEC) dispatch via AHCI PxCI path
      └─ Added identify result handling:
        └─ Parse model string from identify words 27..46
        └─ Export model/status API for shell (`kernel_ahci_identify`, `kernel_ahci_model`)
      └─ CLI expanded:
        └─ `ahci identify [port]` to run identify
        └─ `ahci model` to list cached model per implemented port
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 22:00) ✅

    Phase 35: AHCI DMA Read Path ✅ DONE
      └─ Added AHCI sector-read command path (x86 + x64):
        └─ ATA READ DMA EXT (0x25) with LBA48 fields in H2D Register FIS
        └─ PRDT data buffer wiring and PxCI completion wait with TFES error check
      └─ Added kernel read API: `kernel_ahci_read(port, lba, sectors, out, out_max)`
      └─ Added CLI command:
        └─ `ahci read <port> <lba> [sectors]` (max 8 sectors, prints first bytes as hexdump)
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 22:24) ✅

    Phase 36: AHCI DMA Write Path ✅ DONE
      └─ Added AHCI sector-write command path (x86 + x64):
        └─ ATA WRITE DMA EXT (0x35) with LBA48 fields in H2D Register FIS
        └─ W bit set in CmdHeader (host-to-device direction)
        └─ Caller data copied to DMA buffer before PxCI dispatch
      └─ Added kernel write API: `kernel_ahci_write(port, lba, sectors, data, data_len)`
      └─ Added CLI command:
        └─ `ahci write <port> <lba> <fill-byte-hex>` (fills 1 sector with given byte, e.g. `ahci write 0 1 AA`)
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 22:31) ✅

    Phase 37: AHCI ↔ KFS Backend Integration ✅ DONE
      └─ Made VernisFS sector I/O pluggable:
        └─ Added `vfs_disk_read_fn` / `vfs_disk_write_fn` function pointer typedefs (vernisfs.h)
        └─ Added `vfs_set_disk_ops()` to swap sector backend at runtime
        └─ Default: ATA PIO (0x1F0); when AHCI detected: DMA via `kernel_ahci_read/write`
      └─ All VernisFS operations (superblock/filetable/data R/W) route through `g_disk_read`/`g_disk_write`
      └─ Added `KFS_BACKEND_AHCI = 2` enum in vfs.h
      └─ Added AHCI adapter layer in vfs.c:
        └─ `ahci_sector_read()` / `ahci_sector_write()` — chunk up to 8 sectors per AHCI command
        └─ `kfs_try_ahci()` — auto-detect first identified AHCI port at kfs_init
        └─ `kfs_backend_name()` returns "vernisfs-ahci" when AHCI active
      └─ Boot path: AHCI PCI probe → kfs_try_ahci() → vfs_set_disk_ops() → vfs_init() → VernisFS mounts via DMA
      └─ Fallback: if no AHCI controller, uses ATA PIO transparently
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 22:35) ✅

    Phase 38: NVMe Foundation ✅ DONE
      └─ Added NVMe controller driver (x86 + x64):
        └─ PCI class 01:08 detection at boot
        └─ BAR0 MMIO mapping (16KB for registers + doorbells)
        └─ CAP/VS read, DSTRD doorbell stride calculation
        └─ Controller disable/enable sequence (CC.EN → CSTS.RDY)
        └─ Admin Submission Queue (ASQ) + Admin Completion Queue (ACQ) setup
          └─ AQA = 16 entries, 4K-aligned static buffers
          └─ Phase-bit tracking for CQE completion detection
        └─ Identify Controller command (opcode 0x06, CNS=1)
          └─ Model string (bytes 24-63) + Serial number (bytes 4-23) extraction
      └─ Added NVMe kernel API:
        └─ `kernel_nvme_available()`, `kernel_nvme_version()`
        └─ `kernel_nvme_identified()`, `kernel_nvme_model()`, `kernel_nvme_serial()`
      └─ Added `nvme` CLI command: shows version, model, serial
      └─ Boot-time probe logs NVMe candidate, VS, MQES, model, serial
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 22:41) ✅

    Phase 39: NVMe I/O Queues + Read/Write ✅ DONE
      └─ Added NVMe I/O queue pair (x86 + x64):
        └─ I/O Submission Queue (SQ1) + I/O Completion Queue (CQ1), 16 entries each
        └─ Create IO CQ (admin opcode 0x05, CQID=1, PC=1)
        └─ Create IO SQ (admin opcode 0x01, SQID=1, CQID=1, PC=1)
        └─ Doorbell management: SQ1 tail @ 0x1000+2*stride, CQ1 head @ 0x1000+3*stride
        └─ Phase-bit CQE completion tracking for I/O queue
      └─ Added NVM Read/Write commands:
        └─ NVM Read (opcode 0x02): NSID=1, PRP1→4KB DMA buffer, NLB 0-based
        └─ NVM Write (opcode 0x01): copy data→DMA buffer, same PRP1 path
        └─ `kernel_nvme_read(lba, sectors, out, out_max)` / `kernel_nvme_write(lba, sectors, data, data_len)`
      └─ Updated CLI `nvme` command:
        └─ `nvme read <lba> [sectors]` — reads up to 8 sectors, hexdump output
        └─ `nvme write <lba> <fill-byte-hex>` — fills 1 sector with given byte
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 22:49) ✅

    Phase 40: NVMe ↔ KFS Backend Integration ✅ DONE
      └─ Added `KFS_BACKEND_NVME = 3` enum to vfs.h
      └─ Added NVMe adapter layer in vfs.c:
        └─ `nvme_sector_read()` / `nvme_sector_write()` — chunk up to 8 sectors per NVMe command
        └─ `kfs_try_nvme()` — auto-detect identified NVMe controller
      └─ Updated `kfs_init()` priority: NVMe > AHCI > ATA PIO
        └─ Tries NVMe first, falls back to AHCI, then ATA PIO
      └─ `kfs_backend_name()` returns "vernisfs-nvme" when NVMe active
      └─ NVMe externs added to vfs.c: `kernel_nvme_available`, `kernel_nvme_identified`, `kernel_nvme_read`, `kernel_nvme_write`
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M, 22:58) ✅

    ─── Roadmap: Phase 41-60 (ยังไม่ทำ) ───

    Phase 41: File Descriptor Model ✅ DONE
      └─ Added per-process fd table in x86 + x64 task slots (`fd_table`, `ppid_slot`, `brk`)
      └─ Added syscalls: SYS_OPEN(66), SYS_READ_FD(67), SYS_WRITE_FD(68), SYS_CLOSE(69)
      └─ Added SYS_DUP(70), SYS_DUP2(71), SYS_PIPE(72)
      └─ fd 0/1/2 default to TTY stdin/stdout/stderr via `fd_table_init`
      └─ Added file + TTY + pipe fd backends in syscall path (x86/x64)

    Phase 42: TTY / PTY Subsystem ✅ DONE (TTY core)
      └─ Added kernel TTY abstraction for x86 + x64 (line buffer + cooked/raw input)
      └─ Keyboard IRQ path now feeds TTY input (`tty_push_char*`)
      └─ fd 0 reads from TTY; fd 1/2 writes to VGA/serial via TTY writer
      └─ Added in-kernel pipe ring buffers for `pipe()` read/write fds
      └─ PTY master/slave ยังไม่ทำ (planned for later)

    Phase 43: fork() + exec() ✅ DONE (baseline)
      └─ Added SYS_FORK(73), SYS_EXECVE(74), SYS_SBRK(75) in x86 + x64 syscall dispatch
      └─ `fork`: clones task kernel stack state + fd table, child returns 0
      └─ `execve`: replaces current user image with new ELF from VernisFS
      └─ `sbrk`: per-process heap break growth via page mapping
      └─ CoW page tables + blocking waitpid semantics ยังเป็นงานต่อยอด

    Phase 44: Minimal libc (vernislibc) ✅ DONE (minimal)
      └─ User syscall wrappers in `userlib/syscall.h`: open/read/write/close/dup/pipe/fork/execve/exit/waitpid/getpid/kill/sbrk
      └─ Minimal libc in `userlib/libc.c`: printf/puts + string/memory helpers + malloc/free(bump)
      └─ Added `crt0_x86.asm` + `crt0_x64.asm` (`_start -> main -> SYS_EXIT`)
      └─ Added user linker scripts for x86/x64 at `0x10000000`

    Phase 45: User-Space Shell (/bin/vsh) ✅ DONE (MVP)
      └─ Added user shell program `userlib/vsh.c`
      └─ Reads command line from stdin, supports built-ins: `help`, `cd`, `exit`
      └─ External command flow: `fork` -> `execve` -> parent `waitpid`
      └─ Added cross-build rules for `/bin/vsh32` + `/bin/vsh64` in Makefile
      └─ Updated mkfs tool to embed vsh binaries into VernisFS when present
      └─ Boot path now uses shell-only mode: launch `/bin/vsh32`/`/bin/vsh64` เท่านั้น (ไม่มี auto-fallback ไป hello/exit test)

    Phase 46: mmap + Demand Paging ✅ DONE
      └─ Added VMA (Virtual Memory Area) per-task tracking:
        └─ VmaEntry/VmaEntry32 structs with start, length, type, prot, flags, path, file_offset
        └─ 16 VMAs per task (VMA_MAX_PER_TASK), initialized on task create/fork
        └─ VMA types: VMA_TYPE_ANON (zero-fill) and VMA_TYPE_FILE (VFS-backed)
      └─ Added SYS_MMAP (76) and SYS_MUNMAP (77) syscalls:
        └─ mmap(length, prot_flags, path_ptr): returns virtual address, pages lazily mapped
        └─ munmap(addr, length): removes VMA entry
        └─ Address allocation: bump allocator from MMAP_BASE (0x20000000 x64, 0x30000000 x86)
        └─ Prot flags: PROT_READ(0x01), PROT_WRITE(0x02), PROT_EXEC(0x04)
        └─ Map flags: MAP_ANONYMOUS(0x10), MAP_PRIVATE(0x20)
      └─ Demand paging in #PF handler (vector 14):
        └─ On user-mode page fault, read CR2 and look up VMA list
        └─ Anonymous: allocate frame (pre-zeroed), map with PAGE_USER + optional WRITABLE
        └─ File-backed: allocate frame, read file data from VFS into frame, map
        └─ Resume user execution transparently (return 0 from interrupt_dispatch)
        └─ Falls through to kill if no matching VMA found
      └─ Extended USER_VADDR_MAX to 0x40000000 (1GB) for mmap address space
      └─ Added user-space API in userlib/syscall.h: mmap(), munmap() wrappers
      └─ fork() copies VMA list + mmap_next to child process
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M) ✅

    Phase 47: File Permissions (rwx) ✅ DONE
      └─ VfsFileEntry: added mode(uint16_t), uid(uint16_t), gid(uint16_t) — 6B from reserved
      └─ Unix permission bits: VFS_PERM_UR/UW/UX/GR/GW/GX/OR/OW/OX (0400–0001)
      └─ Defaults: files 0644, dirs 0755; legacy mode=0 treated as permissive
      └─ vfs_chmod(), vfs_chown() with disk flush via vfs_flush_metadata()
      └─ kfs_chmod(), kfs_chown(), kfs_check_perm(path, uid, op) in VFS layer
      └─ Superuser (uid 0) bypasses all permission checks
      └─ chmod <mode> <path>, chown <uid>[:<gid>] <path> CLI commands
      └─ ls -l: drwxrwxrwx uid gid size filename
      └─ Permission checks in cat(r), write(w), append(w), rm(w), exec(x)
      └─ login sets session->uid from userdb index; su saves/restores uid
      └─ userdb_find_uid() maps username → array index (0=root)
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M) ✅

    Phase 48: Disk Buffer Cache ✅ DONE
      └─ LRU write-back block cache (64 blocks × 512B = 32 KB)
      └─ bcache.h / bcache.c: BcacheBlock with lba, flags (valid/dirty), access_tick, data[512]
      └─ Read-through: cache hit returns data; miss → disk read → cache insert → LRU eviction
      └─ Write-back: writes go to cache (dirty flag); flushed on sync/eviction/periodic tick
      └─ Interposed via vfs_set_disk_ops() — captures real backend, installs bcache_read/write
      └─ vfs_get_disk_read/write() getters for safe backend capture
      └─ bcache_tick() called from timer IRQ (240 Hz), auto-flush every ~1s if dirty
      └─ SYS_SYNC (78) syscall flushes all dirty blocks
      └─ `sync` CLI command: shows blocks flushed + hit/miss/writeback/eviction stats
      └─ Build status: x86 ✅, x64 ✅, os.img (4.0M) ✅

    Phase 49: TCP Stack ⬜ PLANNED
      └─ TCP state machine (CLOSED→LISTEN→SYN_SENT→ESTABLISHED→FIN_WAIT→...)
      └─ 3-way handshake (SYN/SYN-ACK/ACK)
      └─ Sequence number tracking, sliding window
      └─ Retransmission timer (basic)
      └─ TCP checksum calculation
      └─ Kernel socket struct (TCB)

    Phase 50: UDP + Socket Layer ⬜ PLANNED
      └─ UDP send/receive (stateless, checksum optional)
      └─ Unified kernel socket API: socket/bind/listen/accept/connect/send/recv
      └─ Port number management (ephemeral ports)
      └─ Socket file descriptors (connect fd model to network)

    Phase 51: DHCP + DNS Client ⬜ PLANNED
      └─ DHCP discover/offer/request/ack flow (UDP 67/68)
      └─ Auto-configure IP, subnet mask, gateway, DNS server
      └─ DNS query (UDP 53) — A record resolution
      └─ /etc/resolv.conf equivalent in VernisFS config
      └─ CLI: `dhcp`, `nslookup <hostname>`

    Phase 52: Userspace Socket API ⬜ PLANNED
      └─ SYS_SOCKET / SYS_BIND / SYS_LISTEN / SYS_ACCEPT / SYS_CONNECT
      └─ SYS_SEND / SYS_RECV / SYS_SENDTO / SYS_RECVFROM
      └─ Socket fd ↔ kernel TCB/UCB mapping
      └─ vernislibc wrappers: socket(), bind(), listen(), accept(), connect(), send(), recv()
      └─ /bin/nc (netcat) user-space tool

    Phase 53: Init Process (PID 1) ⬜ PLANNED
      └─ /sbin/init as first user process (forked by kernel)
      └─ Parse /etc/inittab: respawn getty on tty0
      └─ Reap zombie children (waitpid loop)
      └─ Signal handling: SIGCHLD, SIGTERM for shutdown coordination
      └─ Shutdown path: init sends SIGTERM to all → sync → power off

    Phase 54: Interrupt-Driven I/O ⬜ PLANNED
      └─ AHCI: MSI/MSI-X or legacy IRQ interrupt handler
      └─ NVMe: MSI-X interrupt for CQ completion
      └─ E1000: RX interrupt instead of polling
      └─ Wait queues: process sleeps until IRQ fires, wakes on completion
      └─ Remove busy-wait loops from storage + network paths

    Phase 55: LAPIC + IOAPIC ⬜ PLANNED
      └─ Parse ACPI MADT for LAPIC + IOAPIC entries
      └─ LAPIC MMIO initialization (spurious vector, timer, EOI)
      └─ IOAPIC redirection table setup (remap IRQ0-15)
      └─ Disable legacy 8259 PIC, switch to APIC mode
      └─ Per-CPU LAPIC timer (replace PIT for local scheduling)

    Phase 56: SMP (Multi-Core) ⬜ PLANNED
      └─ AP startup: INIT-SIPI-SIPI sequence
      └─ Per-CPU GDT/IDT/TSS, per-CPU kernel stack
      └─ Per-CPU scheduler run queue
      └─ Spinlock primitives (ticket lock / CAS)
      └─ Kernel big lock → fine-grained locking plan
      └─ IPI for TLB shootdown, cross-CPU scheduling

    Phase 57: USB (xHCI) ⬜ PLANNED
      └─ xHCI controller detection (PCI class 0C:03:30)
      └─ Device Context Base Address Array (DCBAA)
      └─ Command Ring + Event Ring + Transfer Ring
      └─ Port status change detection, device enumeration
      └─ USB mass storage (bulk-only transport) → KFS backend
      └─ USB HID keyboard/mouse (optional)

    Phase 58: Audio (AC97 / Intel HDA) ⬜ PLANNED
      └─ AC97: PCI detect, mixer registers, PCM out via BDL (buffer descriptor list)
      └─ Intel HDA: CORB/RIRB command transport, codec enumeration, PCM stream
      └─ /dev/audio device node
      └─ play CLI command (raw PCM from VernisFS)
      └─ Kernel audio mixer (volume control)

    Phase 59: ext2 / FAT32 Filesystem ⬜ PLANNED
      └─ ext2: superblock, block group descriptors, inode table, directory entries
      └─ FAT32: BPB, FAT chain, directory entries, long file name support
      └─ Mount framework: mount <dev> <path> -t <fstype>
      └─ Multiple mount points in KFS
      └─ Read-write support for at least one external FS

    Phase 60: Multiuser (getty/login) ⬜ PLANNED
      └─ /sbin/getty: open TTY, display login prompt, exec /bin/login
      └─ /bin/login: authenticate against /etc/shadow, setuid, exec shell
      └─ setuid / setgid syscalls
      └─ Per-user home directory (/home/<user>)
      └─ Secure session: umask, environment isolation
```

---

## ภาพรวมสถาปัตยกรรม

```
┌─────────────────────────────────────────────────┐
│                   User Space (Ring 3)           │  ← Phase 17
│   ┌──────────┐  ┌──────────┐  ┌──────────┐     │
│   │  Shell    │  │  Program │  │  Program │     │  ← Phase 19-20
│   └────┬─────┘  └────┬─────┘  └────┬─────┘     │
├────────┼──────────────┼──────────────┼──────────┤
│        │     Syscall Gate (int 0x80 / syscall)  │  ← Phase 17
├────────┼──────────────┼──────────────┼──────────┤
│                   Kernel Space (Ring 0)         │
│                                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│  │Scheduler │  │ Memory   │  │   IPC    │      │  ✅ มีแล้ว (Rust)
│  │(Rust)    │  │ (Rust)   │  │          │      │
│  └──────────┘  └──────────┘  └──────────┘      │
│                                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│  │ VernisFS │  │ Security │  │ AI Engine│      │  ✅ มีแล้ว
│  │ (ATA PIO)│  │ (Policy) │  │ (Rust)   │      │
│  └──────────┘  └──────────┘  └──────────┘      │
│                                                 │
│  ┌──────────────────────────────────────┐       │
│  │  Paging / Virtual Memory Manager    │       │  ← Phase 16
│  └──────────────────────────────────────┘       │
│                                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│  │ Keyboard │  │  Serial  │  │  Timer   │      │  ✅ มีแล้ว
│  │  (IRQ1)  │  │ (COM1/2) │  │  (IRQ0)  │      │
│  └──────────┘  └──────────┘  └──────────┘      │
├─────────────────────────────────────────────────┤
│                  Hardware (x86 / x86_64)        │
└─────────────────────────────────────────────────┘
```
