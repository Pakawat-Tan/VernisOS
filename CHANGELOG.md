# VernisOS Changelog

All notable changes to VernisOS are documented here, organized by development phase.

## [0.1.0-dev] — In Development

### Phase 17 — Prepare Developer Preview
- Added `VERSION` file and Makefile version tracking
- Added `make release`, `make dist`, `make size-report`, `make prerequisites` targets
- Improved `make help` output
- Created developer documentation: GETTING_STARTED, ARCHITECTURE, BUILD, CONTRIBUTING, CHANGELOG
- Expanded README with English sections and links to docs

### Phase 16 — Integration Test & Logging System
- Added structured kernel logging (`klog`) with 6 severity levels and ring buffer
- CLI `log` command: view, clear, filter by level
- Extended self-tests: IPC (6), Sandbox (5), Klog (4), Auditlog (3) — total 39 tests
- Python QEMU integration test suite (9 suites)
- Makefile test targets: `make test`, `make test-x86`, `make test-x64`, `make test-all`

### Phase 15 — Optimize Kernel, Module, Sandbox
- Compiler flags: `-Os`, `-ffunction-sections`, `--gc-sections`
- Rust release profile: LTO, strip, size optimization
- Sandbox: O(N) whitelist → O(1) bitmask check
- IPC: PID-to-queue O(1) cache (`g_pid_to_qid[256]`)
- AI EventStore: `VecDeque` → fixed ring buffer `[Option<EventRecord>; 256]`
- AI event dispatch: string-based → numeric code O(1) lookup
- Scheduler: cached effective priority (avoid recomputation)
- **Binary size: x86 585KB→117KB (-80%), x64 578KB→124KB (-79%)**

### Phase 14 — Test CLI-AI Interaction + Permission System
- VFS access control enforcement
- Auth-integrated file operations
- CLI permission testing with AI policy

### Phase 13 — Kernel AI Policy Enforcement Layer
- VernisFS: sector-based filesystem (superblock + file table + data)
- SHA-256 hash implementation
- User database with password authentication (root/admin/user)
- `login`, `su`, `logout`, `whoami` CLI commands
- Policy enforcement: AI access rules + privilege check
- Denial → AI event feed → anomaly detection

### Phase 12 — Policy System + Config Loader
- Python VPOL policy compiler (`ai/tools/policy_compiler.py`)
- Binary VPOL format: header + modules + access_rules
- Rust policy parser: deserialize VPOL blob into `PolicyConfig`
- C policy loader: ATA PIO read from sector 4096
- CLI `policy` command: show rules and reload

### Phase 11 — AI Auto-Tuning Engine
- Automatic scheduler quantum adjustment based on system load
- In-kernel Rust `AutoTuner`: load tracking + quantum recommendation
- AI response handler: tune/remediate actions
- Callback system: Rust → C kernel (priority/quantum changes)

### Phase 10 — AI Behavior Monitor
- AI engine ported to Rust `no_std` staticlib (was Python)
- `EventStore`: event recording and retrieval
- `AnomalyDetector`: rate/pattern/threshold detection
- `ProcessTracker`: per-PID trust scoring
- AI bridge: C ↔ Rust FFI for event feeding
- CLI `ai` command: status, events, anomalies, trust

### Phase 9 — Python AI Engine (legacy, replaced in Phase 10)
- Initial Python AI listener (deprecated)

### Phase 8 — AI IPC Bridge
- IPC bridge between kernel and AI subsystem
- TCP bridge mode for external AI processing

### Phase 7 — CLI / Terminal System
- Interactive CLI with command parsing
- 10 initial commands (help, clear, info, exit, echo, etc.)
- VGA terminal output + serial debug

### Phase 6 — User Sandbox Environment
- Sandbox module with syscall filtering
- Process type isolation (KERNEL/SYSTEM/USER)
- Syscall whitelist per sandbox level

### Phase 5 — Module Loader + Dynamic Linking
- Module loader framework
- `SYSCALL`/`SYSRET` stubs (x64)

### Phase 4 — Inter-process Communication (IPC)
- Message queue system (16 queues, 32 messages each)
- Channel-based IPC (create, send, recv, close)
- IPC syscalls (numbers 20-25)

### Phase 3 — Core Kernel (Microkernel)
- Rust `no_std` kernel core: scheduler, memory, syscall dispatcher
- C kernel for x86 and x64 architectures
- GDT, IDT, PIC, PIT (100Hz), keyboard driver
- Serial and VGA terminal output
- C ↔ Rust FFI bridge

### Phase 2 — Bootloader (BIOS)
- 3-stage bootloader (NASM)
- Runtime CPUID detection: x86 vs x86_64
- MBR → Protected Mode → Long Mode (conditional)
- ATA PIO disk read for kernel loading

### Phase 1 — Architecture & Documentation
- Initial architecture design
- Development plan and phase structure
