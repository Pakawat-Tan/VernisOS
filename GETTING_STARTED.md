# Getting Started with VernisOS

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| `i686-elf-gcc` | 12+ | x86 cross-compiler |
| `x86_64-elf-gcc` | 12+ | x64 cross-compiler |
| `nasm` | 2.15+ | Assembler (bootloader) |
| `cargo` (nightly) | 1.75+ | Rust compiler (kernel core) |
| `qemu-system-x86_64` | 7.0+ | Emulator (optional, for testing) |
| `python3` | 3.10+ | Integration tests (optional) |

> Run `make prerequisites` to verify your toolchain.

### macOS (Homebrew)

```bash
brew install nasm qemu
brew install x86_64-elf-gcc i686-elf-gcc
rustup install nightly
rustup component add rust-src --toolchain nightly
```

### Ubuntu / Debian

```bash
sudo apt install nasm qemu-system-x86 build-essential
# Cross-compiler: build from source or use a prebuilt toolchain
# See: https://wiki.osdev.org/GCC_Cross-Compiler
rustup install nightly
rustup component add rust-src --toolchain nightly
```

## Quick Start

```bash
# 1. Clone the repository
git clone <repo-url> VernisOS
cd VernisOS

# 2. Check prerequisites
make prerequisites

# 3. Build everything
make

# 4. Run in QEMU (x64)
make run64

# 5. Run in QEMU (x86)
make run32
```

## First Boot

When VernisOS boots, you'll see a CLI prompt:

```
root@vernisOS> _
```

You are logged in as **root** (privilege level 0). Try these commands:

```
help        — List all 18 available commands
info        — System information (uptime, processes, AI status)
ps          — Process list with CPU/memory usage
ai status   — AI engine status and event count
test        — Run kernel self-tests (SHA-256, IPC, sandbox, etc.)
log         — View kernel log entries
users       — List system users
```

### User Sessions

```
login       — Switch to another user (admin/user)
logout      — Return to root
su <cmd>    — Run a single command as root
whoami      — Show current user and privilege level
```

### Privilege Levels

| Level | Role | Access |
|-------|------|--------|
| 0 | root | All commands (shutdown, restart, policy, test) |
| 50 | admin | AI queries, user management, audit log |
| 100 | user | Basic commands (help, info, ps, echo) |

## Testing

```bash
# Run integration tests (requires QEMU)
make test           # x64 tests
make test-x86       # x86 tests
make test-all       # Both architectures
```

## Release Build

```bash
make release        # Clean build + size report
make dist           # Package into dist/VernisOS-x.x.x.tar.gz
```

## Next Steps

- [BUILD.md](docs/BUILD.md) — Detailed build system documentation
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — System architecture overview
- [CONTRIBUTING.md](CONTRIBUTING.md) — How to contribute
- [CHANGELOG.md](CHANGELOG.md) — Version history
