# Contributing to VernisOS

## Phase Discipline

VernisOS follows a strict **phased development plan**. Each phase specifies allowed languages:

| Phase | Languages |
|-------|-----------|
| Phase 2 | Assembly (NASM), C |
| Phase 3 | C, Rust |
| Phase 4 | C only |
| Phase 7 | C only |
| Phase 10-11 | Rust (AI engine) |
| Phase 12 | Python (policy tools) |
| Phase 13 | C |
| Phase 15 | Rust, C |
| Phase 17 | Makefile, Doc tools |

**Rule**: Do not use a language outside its designated phase.

## Project Structure

```
VernisOS/
├── boot/x86/           # Bootloader (Assembly)
├── kernel/
│   ├── arch/x86/       # x86 kernel (C)
│   ├── arch/x86_64/    # x64 kernel (C)
│   ├── core/verniskernel/  # Rust AI engine + scheduler
│   ├── drivers/        # Hardware drivers (ai_bridge, etc.)
│   ├── fs/             # Filesystem (vernisfs, vfs)
│   ├── ipc/            # Inter-process communication
│   ├── log/            # Logging (klog, auditlog)
│   ├── module/         # Module loader
│   ├── security/       # Sandbox, policy enforcement
│   ├── selftest/       # Kernel self-tests
│   ├── shell/          # CLI and terminal
│   └── shims/          # Compatibility shims
├── include/            # C headers
├── lib/
│   ├── x86/            # x86 static libraries
│   └── x86_64/         # x64 static libraries
├── ai/
│   ├── tools/          # Python tools (policy compiler, mkfs)
│   └── tests/          # Integration test suite
├── docs/               # Documentation
└── Makefile
```

## Code Style

### C
- GNU11 standard (`-std=gnu11`)
- `snake_case` for functions and variables
- Prefix module functions: `ipc_send()`, `sandbox_check()`, `klog_info()`
- Use `#pragma once` for headers
- Comment only when clarification is needed

### Rust
- Standard Rust conventions
- `snake_case` for functions, `PascalCase` for types
- `no_std` — no standard library, only `core` and `alloc`
- Use `extern "C"` for FFI exports

### Assembly (NASM)
- Intel syntax
- Comment every non-obvious instruction
- Label format: `section_name.label`

## Build & Test

```bash
# Check prerequisites
make prerequisites

# Build
make                # Full build (both architectures)
make build-x86      # x86 only
make build-x64      # x64 only

# Test
make run64          # Manual testing in QEMU
make test           # Automated integration tests

# Verify
make size-report    # Check binary sizes (must stay under boot load limits)
```

## Binary Size Limits

| Kernel | Max Size | Boot Loads |
|--------|----------|-----------|
| x86 | ~600 KB | 1024 sectors from sector 12 |
| x64 | ~768 KB | via Stage 3 chunk loader |

Always check binary size after changes: `make size-report`

## Adding a New CLI Command

1. Add forward declaration in `kernel/shell/cli.c`
2. Add entry to `commands[]` table with privilege level
3. Increment `BUILTIN_COMMANDS_COUNT`
4. Implement the handler function
5. Add klog call at command entry

## Adding a New Kernel Module

1. Create `kernel/<subsystem>/module.c` + `include/module.h`
2. Add Makefile variables and compile rules for both architectures
3. `#include` in both `kernel_x86.c` and `kernel_x64.c`
4. Add init call to `kernel_main()` (respect init order!)
5. Add self-tests in `selftest.c`

## Reporting Issues

When reporting a bug, include:
- Which architecture: x86 or x64
- Exception info if applicable (vector number, RIP, error code)
- Serial output up to the crash point
- `make size-report` output
