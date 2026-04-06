# ==== Pattern rule for syscall.asm (x86) ====
make/kernel/arch/x86/syscall.o: kernel/arch/x86/syscall.asm | prepare
	nasm -f elf32 -o $@ $<
# ==== Pattern rules for shell object files (x86) ====
make/kernel/arch/x86/%.o: kernel/shell/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Pattern rules for shell object files (x86_64) ====
make/kernel/arch/x86_64/%.o: kernel/shell/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@
# ==== Pattern rules for additional kernel object files (x86) ====
make/kernel/arch/x86/%.o: kernel/module/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@
make/kernel/arch/x86/%.o: kernel/log/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Pattern rules for additional kernel object files (x86_64) ====
make/kernel/arch/x86_64/%.o: kernel/module/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@
make/kernel/arch/x86_64/%.o: kernel/log/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@
# ==== Pattern rules for kernel assembly object files (x86) ====
make/kernel/arch/x86/%.o: kernel/arch/x86/%.asm | prepare
	nasm -f elf32 -o $@ $<

# ==== Pattern rules for kernel assembly object files (x86_64) ====
make/kernel/arch/x86_64/%.o: kernel/arch/x86_64/%.asm | prepare
	nasm -f elf64 -o $@ $<
# ==== Pattern rules for kernel object files (x86) ====
make/kernel/arch/x86/%.o: kernel/arch/x86/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86/%.o: kernel/drivers/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86/%.o: kernel/security/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86/%.o: kernel/fs/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86/%.o: kernel/selftest/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86/%.o: kernel/shims/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86/%.o: kernel/net/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86/%.o: kernel/ipc/%.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Pattern rules for kernel object files (x86_64) ====
make/kernel/arch/x86_64/%.o: kernel/arch/x86_64/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86_64/%.o: kernel/drivers/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86_64/%.o: kernel/security/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86_64/%.o: kernel/fs/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86_64/%.o: kernel/selftest/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86_64/%.o: kernel/shims/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86_64/%.o: kernel/net/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86_64/%.o: kernel/ipc/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

make/kernel/arch/x86_64/%.o: kernel/net/%.c | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# VernisOS Makefile — Microkernel OS with AI Core
VERSION    := $(shell cat VERSION 2>/dev/null || echo "0.0.0-unknown")
BUILD_DATE := $(shell date +%Y-%m-%d)

# Boot stages
STAGE1 = boot/x86/stage1.asm
STAGE2 = boot/x86/stage2.asm
STAGE3 = boot/x86/stage3.asm

# Kernel sources
KERNEL_X86_SRC = kernel/arch/x86/kernel_x86.c
KERNEL_X64_SRC = kernel/arch/x86_64/kernel_x64.c

# Core library
CORE_LIB_DIR = kernel/core/verniskernel
CORE_LIB = $(CORE_LIB_DIR)


# VernisOS Rust staticlib
VERNISOS_DIR = lib
VERNISOS_LIB = $(VERNISOS_DIR)/x86/libvernisos.a
VERNISOS_LIB_X64 = $(VERNISOS_DIR)/x86_64/libvernisos_x64.a
VERNISOS_INC = -I include

# Kernel linker
KERNEL_X86_LD = kernel/arch/x86/linker.ld
KERNEL_X64_LD = kernel/arch/x86_64/linker.ld

# Boot output
STAGE1_BIN = make/boot/x86/stage1.bin
STAGE2_BIN = make/boot/x86/stage2.bin
STAGE3_BIN = make/boot/x86/stage3.bin

KERNEL_X86_TCP = make/kernel/arch/x86/tcp.o
KERNEL_X64_TCP = make/kernel/arch/x86_64/tcp.o
# Add tcp.o to kernel object lists
KERNEL_X86_OBJ := make/kernel/arch/x86/kernel_x86.o \
	make/kernel/arch/x86/rust_shims.o \
	make/kernel/arch/x86/interrupts.o \
	make/kernel/arch/x86/syscall.o \
	make/kernel/arch/x86/ipc.o \
	make/kernel/arch/x86/module.o \
	make/kernel/arch/x86/dylib.o \
	make/kernel/arch/x86/sandbox.o \
	make/kernel/arch/x86/cli.o \
	make/kernel/arch/x86/ai_bridge.o \
	make/kernel/arch/x86/ai_engine.o \
	make/kernel/arch/x86/acpi.o \
	make/kernel/arch/x86/policy_loader.o \
	make/kernel/arch/x86/sha256.o \
	make/kernel/arch/x86/vernisfs.o \
	make/kernel/arch/x86/vfs.o \
	make/kernel/arch/x86/userdb.o \
	make/kernel/arch/x86/policy_enforce.o \
	make/kernel/arch/x86/selftest.o \
	make/kernel/arch/x86/auditlog.o \
	make/kernel/arch/x86/klog.o \
	make/kernel/arch/x86/bcache.o \
	make/kernel/arch/x86/tcp.o
# ==== Compile libc.c (x86) ====
userlib/libc.o: userlib/libc.c userlib/libc.h | prepare
	$(CC_X86) $(CFLAGS_X86) -c $< -o $@
KERNEL_X64_SHIM = make/kernel/arch/x86_64/rust_shims.o
KERNEL_X64_INTR = make/kernel/arch/x86_64/interrupts.o
KERNEL_X64_SYSC = make/kernel/arch/x86_64/syscall.o
KERNEL_X64_IPC  = make/kernel/arch/x86_64/ipc.o
KERNEL_X64_MOD  = make/kernel/arch/x86_64/module.o
KERNEL_X64_DLIB = make/kernel/arch/x86_64/dylib.o
KERNEL_X64_SAND = make/kernel/arch/x86_64/sandbox.o
KERNEL_X64_CLI  = make/kernel/arch/x86_64/cli.o
KERNEL_X64_AI   = make/kernel/arch/x86_64/ai_bridge.o
KERNEL_X64_AIE  = make/kernel/arch/x86_64/ai_engine.o
KERNEL_X64_ACPI = make/kernel/arch/x86_64/acpi.o
KERNEL_X64_POL  = make/kernel/arch/x86_64/policy_loader.o
KERNEL_X64_SHA  = make/kernel/arch/x86_64/sha256.o
KERNEL_X64_VFS  = make/kernel/arch/x86_64/vernisfs.o
KERNEL_X64_KVFS = make/kernel/arch/x86_64/vfs.o
KERNEL_X64_UDB  = make/kernel/arch/x86_64/userdb.o
KERNEL_X64_ENF  = make/kernel/arch/x86_64/policy_enforce.o
KERNEL_X64_TST  = make/kernel/arch/x86_64/selftest.o
KERNEL_X64_AUD  = make/kernel/arch/x86_64/auditlog.o
KERNEL_X64_KLOG = make/kernel/arch/x86_64/klog.o
KERNEL_X64_BCACHE = make/kernel/arch/x86_64/bcache.o
KERNEL_X64_OBJ  = make/kernel/arch/x86_64/kernel_x64.o $(KERNEL_X64_SHIM) $(KERNEL_X64_INTR) $(KERNEL_X64_SYSC) $(KERNEL_X64_IPC) $(KERNEL_X64_MOD) $(KERNEL_X64_DLIB) $(KERNEL_X64_SAND) $(KERNEL_X64_CLI) $(KERNEL_X64_AI) $(KERNEL_X64_AIE) $(KERNEL_X64_ACPI) $(KERNEL_X64_POL) $(KERNEL_X64_SHA) $(KERNEL_X64_VFS) $(KERNEL_X64_KVFS) $(KERNEL_X64_UDB) $(KERNEL_X64_ENF) $(KERNEL_X64_TST) $(KERNEL_X64_AUD) $(KERNEL_X64_KLOG) $(KERNEL_X64_BCACHE) $(KERNEL_X64_TCP)

KERNEL_X86_ELF = make/kernel/arch/x86/kernel_x86.elf
KERNEL_X64_ELF = make/kernel/arch/x86_64/kernel_x64.elf

KERNEL_X86_BIN = make/kernel/arch/x86/kernel_x86.bin
KERNEL_X64_BIN = make/kernel/arch/x86_64/kernel_x64.bin

UNIVERSAL_IMG = os.img
POLICY_YAML = ai/config/policy.yaml
POLICY_BIN  = make/policy.bin
POLICY_SECTOR = 4096
VFS_BIN     = make/vernisfs.bin
VFS_SECTOR  = 5120

# User-space programs (Phase 44/45)
USER_DIR = userlib
USER_OUT_DIR = make/user
USER_VSH_SRC = $(USER_DIR)/vsh.c
USER_LIBC_SRC = $(USER_DIR)/libc.c
USER_SYSCALL_HDR = $(USER_DIR)/syscall.h
USER_LIBC_HDR = $(USER_DIR)/libc.h
USER_CRT0_X86 = $(USER_DIR)/crt0_x86.asm
USER_CRT0_X64 = $(USER_DIR)/crt0_x64.asm
USER_LD_X86 = $(USER_DIR)/linker_x86.ld
USER_LD_X64 = $(USER_DIR)/linker_x64.ld

USER_CFLAGS_COMMON = -Wall -Wextra -Os -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-omit-frame-pointer -I $(USER_DIR)
USER_CFLAGS_X86 = $(USER_CFLAGS_COMMON) -m32 -march=i686
USER_CFLAGS_X64 = $(USER_CFLAGS_COMMON) -mno-red-zone

USER_CRT0_X86_O = $(USER_OUT_DIR)/crt0_x86.o
USER_CRT0_X64_O = $(USER_OUT_DIR)/crt0_x64.o
USER_LIBC_X86_O = $(USER_OUT_DIR)/libc_x86.o
USER_LIBC_X64_O = $(USER_OUT_DIR)/libc_x64.o
USER_VSH_X86_O  = $(USER_OUT_DIR)/vsh_x86.o
USER_VSH_X64_O  = $(USER_OUT_DIR)/vsh_x64.o
USER_VSH_X86_ELF = $(USER_OUT_DIR)/vsh32.elf
USER_VSH_X64_ELF = $(USER_OUT_DIR)/vsh64.elf

# ==== Toolchains ====
CC_X86 = i686-elf-gcc
CC_X64 = x86_64-elf-gcc

LD_X86 = i686-elf-ld
LD_X64 = x86_64-elf-ld

OBJCOPY_X86 = i686-elf-objcopy
OBJCOPY_X64 = x86_64-elf-objcopy

# Rust target for core library
RUST_TARGET = i386.json
RUST_TARGET_X64 = x86_64-unknown-none

# ==== Flags ====
CFLAGS = -Wall -Wextra -Os -std=gnu99 -ffreestanding -fno-pie -ffunction-sections -fdata-sections
CFLAGS_X86 = $(CFLAGS) -m32 -march=i686 -Wa,--noexecstack
CFLAGS_X64 = $(CFLAGS) -mno-red-zone -mcmodel=kernel \
             -mgeneral-regs-only
CFLAGS_ARM64 = $(CFLAGS) -march=armv8-a -g

.PHONY: all clean run \
        build-x86 build-x64 build-arm64 bulid-x86 bulid-x64 \
        run32 run64 run-arm64 debug show prepare help \
        build-core

PLATFORM ?= all

all: prepare build-core $(UNIVERSAL_IMG)

# ==== Prepare directories ====
prepare:
	mkdir -p make/boot/x86
	mkdir -p make/kernel/arch/x86
	mkdir -p make/kernel/arch/x86_64
	mkdir -p make/user
	mkdir -p lib/x86 lib/x86_64

# ==== Build core library ====
build-core: prepare $(VERNISOS_LIB) $(VERNISOS_LIB_X64)

# Force-rebuild Rust library when sources change (phony targets)
.PHONY: rust rust32 rust64

rust32: | prepare
	@echo "Rebuilding Rust core library (x86) — forcing clean..."
	cd $(CORE_LIB_DIR) && cargo +nightly clean --target $(RUST_TARGET)
	cd $(CORE_LIB_DIR) && cargo +nightly build -Zbuild-std=core,alloc -Zjson-target-spec --target $(RUST_TARGET) --release
	cp $(CORE_LIB_DIR)/target/i386/release/libvernisos.a $(VERNISOS_DIR)/x86/libvernisos.a
	i686-elf-ranlib $(VERNISOS_LIB)

rust64: | prepare
	@echo "Rebuilding Rust core library (x86_64) — forcing clean..."
	cd $(CORE_LIB_DIR) && cargo +nightly clean --target $(RUST_TARGET_X64)
	cd $(CORE_LIB_DIR) && RUSTFLAGS="-C no-redzone=yes" cargo +nightly build -Zbuild-std=core,alloc --target $(RUST_TARGET_X64) --release
	cp $(CORE_LIB_DIR)/target/$(RUST_TARGET_X64)/release/libvernisos.a $(VERNISOS_LIB_X64)
	x86_64-elf-ranlib $(VERNISOS_LIB_X64)

rust: rust32 rust64

$(VERNISOS_LIB):
	@echo "Building core library (x86)..."
	cd $(CORE_LIB_DIR) && cargo +nightly build -Zbuild-std=core,alloc -Zjson-target-spec --target $(RUST_TARGET) --release
	cp $(CORE_LIB_DIR)/target/i386/release/libvernisos.a $(VERNISOS_DIR)/x86/libvernisos.a
	i686-elf-ranlib $@

$(VERNISOS_LIB_X64): | prepare
	@echo "Building core library (x86_64)..."
	cd $(CORE_LIB_DIR) && RUSTFLAGS="-C no-redzone=yes" cargo +nightly build -Zbuild-std=core,alloc --target $(RUST_TARGET_X64) --release
	cp $(CORE_LIB_DIR)/target/$(RUST_TARGET_X64)/release/libvernisos.a $(VERNISOS_LIB_X64)
	x86_64-elf-ranlib $@

# ==== Kernel Build Targets ====
build-x86: prepare $(VERNISOS_LIB) $(KERNEL_X86_BIN)
build-x64: prepare $(VERNISOS_LIB_X64) $(KERNEL_X64_BIN)

build-kernels:
ifeq ($(PLATFORM),all)
	$(MAKE) build-x86
	$(MAKE) build-x64
else ifeq ($(PLATFORM),x86)
	$(MAKE) build-x86
else ifeq ($(PLATFORM),x64)
	$(MAKE) build-x64
endif

bulid-x86: build-x86
bulid-x64: build-x64

# ==== Universal Image ====
$(UNIVERSAL_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(STAGE3_BIN) \
                  $(KERNEL_X86_BIN) $(KERNEL_X64_BIN) $(POLICY_BIN) $(VFS_BIN)

	@echo "Creating universal image..."
	rm -f $@
	dd if=/dev/zero of=$@ bs=1M count=4 status=none

	@echo "Adding x86 bootloader..."
	dd if=$(STAGE1_BIN) of=$@ bs=512 count=1 conv=notrunc
	dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc
	dd if=$(STAGE3_BIN) of=$@ bs=512 seek=7 conv=notrunc

	@echo "Adding x86 kernel..."
	dd if=$(KERNEL_X86_BIN) of=$@ bs=512 seek=13 conv=notrunc

	@echo "Adding x64 kernel..."
	dd if=$(KERNEL_X64_BIN) of=$@ bs=512 seek=2048 conv=notrunc

	@echo "Adding policy blob at sector $(POLICY_SECTOR)..."
	dd if=$(POLICY_BIN) of=$@ bs=512 seek=$(POLICY_SECTOR) conv=notrunc

	@echo "Adding VernisFS at sector $(VFS_SECTOR)..."
	dd if=$(VFS_BIN) of=$@ bs=512 seek=$(VFS_SECTOR) conv=notrunc


	@echo "Done: $(UNIVERSAL_IMG)"
	ls -lh $@

# ==== Policy blob ====
$(POLICY_BIN): $(POLICY_YAML) ai/tools/policy_compile.py | prepare
	@echo "Compiling policy YAML -> binary..."
	python3 ai/tools/policy_compile.py $(POLICY_YAML) -o $(POLICY_BIN)

# ==== Boot stages ====
$(STAGE1_BIN): $(STAGE1) | prepare
	nasm -f bin -o $@ $<

$(STAGE2_BIN): $(STAGE2) | prepare
	nasm -f bin -o $@ $<
	truncate -s 2560 $@

$(STAGE3_BIN): $(STAGE3) | prepare
	nasm -f bin -o $@ $<
	truncate -s 3072 $@

# ==== Compile kernel_x86.c ====
make/kernel/arch/x86/kernel_x86.o: kernel/arch/x86/kernel_x86.c | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile rust_shims.c (x86) ====
$(KERNEL_X86_SHIM): kernel/shims/rust_shims.c | prepare
	$(CC_X86) $(CFLAGS_X86) -c $< -o $@

# ==== Compile ipc.c (x86) ====
$(KERNEL_X86_IPC): kernel/ipc/ipc.c include/ipc.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile interrupts.asm (x86) ====
$(KERNEL_X86_INTR): kernel/arch/x86/interrupts.asm | prepare
	nasm -f elf32 -o $@ $<

# ==== Kernel compilation (link all .o) ====
$(KERNEL_X86_BIN): $(KERNEL_X86_OBJ) $(KERNEL_X86_LD) $(VERNISOS_LIB)
	$(LD_X86) -T $(KERNEL_X86_LD) -m elf_i386 --gc-sections $(KERNEL_X86_OBJ) $(VERNISOS_LIB) -o $(KERNEL_X86_ELF)
	$(OBJCOPY_X86) -O binary $(KERNEL_X86_ELF) $@

# ==== Compile support files for x64 ====
$(KERNEL_X64_SHIM): kernel/shims/rust_shims.c | prepare
	$(CC_X64) $(CFLAGS_X64) -c $< -o $@

$(KERNEL_X64_INTR): kernel/arch/x86_64/interrupts.asm | prepare
	nasm -f elf64 -o $@ $<

$(KERNEL_X64_SYSC): kernel/arch/x86_64/syscall.asm | prepare
	nasm -f elf64 -o $@ $<

$(KERNEL_X64_IPC): kernel/ipc/ipc.c include/ipc.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -Wa,--noexecstack -c $< -o $@

# ==== Compile module.c (x86) ====
$(KERNEL_X86_MOD): kernel/module/module.c include/module.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -Wa,--noexecstack -c $< -o $@

# ==== Compile module.c (x64) ====
$(KERNEL_X64_MOD): kernel/module/module.c include/module.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -Wa,--noexecstack -c $< -o $@

# ==== Compile dylib.c (x86) ====
$(KERNEL_X86_DLIB): kernel/module/dylib.c include/dylib.h include/module.h include/vfs.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -Wa,--noexecstack -c $< -o $@

# ==== Compile dylib.c (x64) ====
$(KERNEL_X64_DLIB): kernel/module/dylib.c include/dylib.h include/module.h include/vfs.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -Wa,--noexecstack -c $< -o $@

# ==== Compile sandbox.c (x86) ====
$(KERNEL_X86_SAND): kernel/security/sandbox.c include/sandbox.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile sandbox.c (x64) ====
$(KERNEL_X64_SAND): kernel/security/sandbox.c include/sandbox.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile cli.c (x86) ====
$(KERNEL_X86_CLI): kernel/shell/cli.c include/cli.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile cli.c (x64) ====
$(KERNEL_X64_CLI): kernel/shell/cli.c include/cli.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile ai_bridge.c (x86) ====
$(KERNEL_X86_AI): kernel/drivers/ai_bridge.c include/ai_bridge.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile ai_engine.c (x86) ====
$(KERNEL_X86_AIE): kernel/drivers/ai_engine.c include/ai_bridge.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile acpi.c (x86) ====
$(KERNEL_X86_ACPI): kernel/drivers/acpi.c include/acpi.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile ai_bridge.c (x64) ====
$(KERNEL_X64_AI): kernel/drivers/ai_bridge.c include/ai_bridge.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile ai_engine.c (x64) ====
$(KERNEL_X64_AIE): kernel/drivers/ai_engine.c include/ai_bridge.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile acpi.c (x64) ====
$(KERNEL_X64_ACPI): kernel/drivers/acpi.c include/acpi.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== VernisFS image ====
$(VFS_BIN): ai/tools/mkfs_vernis.py $(USER_VSH_X86_ELF) $(USER_VSH_X64_ELF) | prepare
	@echo "Creating VernisFS image..."
	python3 ai/tools/mkfs_vernis.py -o $(VFS_BIN) --vsh32 $(USER_VSH_X86_ELF) --vsh64 $(USER_VSH_X64_ELF)

# ==== User-space programs (Phase 44/45) ====
$(USER_CRT0_X86_O): $(USER_CRT0_X86) | prepare
	nasm -f elf32 -o $@ $<

$(USER_CRT0_X64_O): $(USER_CRT0_X64) | prepare
	nasm -f elf64 -o $@ $<

$(USER_LIBC_X86_O): $(USER_LIBC_SRC) $(USER_SYSCALL_HDR) $(USER_LIBC_HDR) | prepare
	$(CC_X86) $(USER_CFLAGS_X86) -c $< -o $@

$(USER_LIBC_X64_O): $(USER_LIBC_SRC) $(USER_SYSCALL_HDR) $(USER_LIBC_HDR) | prepare
	$(CC_X64) $(USER_CFLAGS_X64) -c $< -o $@

$(USER_VSH_X86_O): $(USER_VSH_SRC) $(USER_SYSCALL_HDR) $(USER_LIBC_HDR) | prepare
	$(CC_X86) $(USER_CFLAGS_X86) -c $< -o $@

$(USER_VSH_X64_O): $(USER_VSH_SRC) $(USER_SYSCALL_HDR) $(USER_LIBC_HDR) | prepare
	$(CC_X64) $(USER_CFLAGS_X64) -c $< -o $@

$(USER_VSH_X86_ELF): $(USER_CRT0_X86_O) $(USER_LIBC_X86_O) $(USER_VSH_X86_O) $(USER_LD_X86) | prepare
	$(LD_X86) -T $(USER_LD_X86) -m elf_i386 --gc-sections $(USER_CRT0_X86_O) $(USER_LIBC_X86_O) $(USER_VSH_X86_O) -o $@

$(USER_VSH_X64_ELF): $(USER_CRT0_X64_O) $(USER_LIBC_X64_O) $(USER_VSH_X64_O) $(USER_LD_X64) | prepare
	$(LD_X64) -T $(USER_LD_X64) -m elf_x86_64 --gc-sections $(USER_CRT0_X64_O) $(USER_LIBC_X64_O) $(USER_VSH_X64_O) -o $@

# ==== Compile policy_loader.c (x86) ====
$(KERNEL_X86_POL): kernel/security/policy_loader.c include/ai_bridge.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile policy_loader.c (x64) ====
$(KERNEL_X64_POL): kernel/security/policy_loader.c include/ai_bridge.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile sha256.c (x86) ====
$(KERNEL_X86_SHA): kernel/security/sha256.c include/sha256.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile sha256.c (x64) ====
$(KERNEL_X64_SHA): kernel/security/sha256.c include/sha256.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile vernisfs.c (x86) ====
$(KERNEL_X86_VFS): kernel/fs/vernisfs.c include/vernisfs.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile vernisfs.c (x64) ====
$(KERNEL_X64_VFS): kernel/fs/vernisfs.c include/vernisfs.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile vfs abstraction (x86) ====
$(KERNEL_X86_KVFS): kernel/fs/vfs.c include/vfs.h include/vernisfs.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile vfs abstraction (x64) ====
$(KERNEL_X64_KVFS): kernel/fs/vfs.c include/vfs.h include/vernisfs.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile bcache.c (x86) ====
$(KERNEL_X86_BCACHE): kernel/fs/bcache.c include/bcache.h include/vernisfs.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile bcache.c (x64) ====
$(KERNEL_X64_BCACHE): kernel/fs/bcache.c include/bcache.h include/vernisfs.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile userdb.c (x86) ====
$(KERNEL_X86_UDB): kernel/security/userdb.c include/userdb.h include/sha256.h include/vfs.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile userdb.c (x64) ====
$(KERNEL_X64_UDB): kernel/security/userdb.c include/userdb.h include/sha256.h include/vfs.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile policy_enforce.c (x86) ====
$(KERNEL_X86_ENF): kernel/security/policy_enforce.c include/policy_enforce.h include/ai_bridge.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile policy_enforce.c (x64) ====
$(KERNEL_X64_ENF): kernel/security/policy_enforce.c include/policy_enforce.h include/ai_bridge.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile selftest.c (x86) ====
$(KERNEL_X86_TST): kernel/selftest/selftest.c include/selftest.h include/sha256.h include/userdb.h include/policy_enforce.h include/ai_bridge.h include/ipc.h include/sandbox.h include/klog.h include/auditlog.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile selftest.c (x64) ====
$(KERNEL_X64_TST): kernel/selftest/selftest.c include/selftest.h include/sha256.h include/userdb.h include/policy_enforce.h include/ai_bridge.h include/ipc.h include/sandbox.h include/klog.h include/auditlog.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile auditlog.c (x86) ====
$(KERNEL_X86_AUD): kernel/security/auditlog.c include/auditlog.h include/cli.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile auditlog.c (x64) ====
$(KERNEL_X64_AUD): kernel/security/auditlog.c include/auditlog.h include/cli.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

# ==== Compile klog.c (x86) ====
$(KERNEL_X86_KLOG): kernel/log/klog.c include/klog.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile klog.c (x64) ====
$(KERNEL_X64_KLOG): kernel/log/klog.c include/klog.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

$(KERNEL_X64_BIN): $(KERNEL_X64_SRC) $(KERNEL_X64_LD) $(KERNEL_X64_SHIM) \
	   $(KERNEL_X64_INTR) $(KERNEL_X64_SYSC) $(KERNEL_X64_IPC) \
	   $(KERNEL_X64_MOD) $(KERNEL_X64_DLIB) $(KERNEL_X64_SAND) $(KERNEL_X64_CLI) $(KERNEL_X64_AI) $(KERNEL_X64_AIE) $(KERNEL_X64_ACPI) $(KERNEL_X64_POL) \
	   $(KERNEL_X64_SHA) $(KERNEL_X64_VFS) $(KERNEL_X64_KVFS) $(KERNEL_X64_UDB) $(KERNEL_X64_ENF) $(KERNEL_X64_TST) $(KERNEL_X64_AUD) $(KERNEL_X64_KLOG) $(KERNEL_X64_BCACHE) $(KERNEL_X64_TCP) $(VERNISOS_LIB_X64) | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $(KERNEL_X64_SRC) -o make/kernel/arch/x86_64/kernel_x64.o
	$(LD_X64) -T $(KERNEL_X64_LD) -m elf_x86_64 --gc-sections \
	          $(KERNEL_X64_OBJ) $(VERNISOS_LIB_X64) -o $(KERNEL_X64_ELF)
	$(OBJCOPY_X64) -O binary $(KERNEL_X64_ELF) $@

# ==== Run/Debug ====
run32: $(UNIVERSAL_IMG)
	qemu-system-i386 -drive format=raw,file=$(UNIVERSAL_IMG)

run64: $(UNIVERSAL_IMG)
	qemu-system-x86_64 -drive format=raw,file=$(UNIVERSAL_IMG)

# Run x64 with AI bridge on TCP port 4444
# Start Python listener first: python3 ai/ai_listener.py --port 4444
run64-ai: $(UNIVERSAL_IMG)
	qemu-system-x86_64 -drive format=raw,file=$(UNIVERSAL_IMG) \
	    -serial stdio \
	    -chardev socket,id=ai,host=localhost,port=4444,server=off \
	    -device isa-serial,chardev=ai,index=1

debug32: $(UNIVERSAL_IMG)
	@echo "Use GDB with: target remote :1234"
	qemu-system-i386 -s -S -drive format=raw,file=$(UNIVERSAL_IMG)

debug64: $(UNIVERSAL_IMG)
	@echo "Use GDB with: target remote :1234"
	qemu-system-x86_64 -s -S -drive format=raw,file=$(UNIVERSAL_IMG)

# ==== Test Targets ====
test: test-x64

test-x64: $(UNIVERSAL_IMG)
	@echo "Running integration tests (x64)..."
	python3 ai/tests/test_integration.py --arch x64 --img $(UNIVERSAL_IMG)

test-x86: $(UNIVERSAL_IMG)
	@echo "Running integration tests (x86)..."
	python3 ai/tests/test_integration.py --arch x86 --img $(UNIVERSAL_IMG)

test-all: $(UNIVERSAL_IMG)
	@echo "Running integration tests (x86)..."
	python3 ai/tests/test_integration.py --arch x86 --img $(UNIVERSAL_IMG)
	@echo ""
	@echo "Running integration tests (x64)..."
	python3 ai/tests/test_integration.py --arch x64 --img $(UNIVERSAL_IMG)

# ==== Release & Dev Preview Targets ====
.PHONY: version size-report prerequisites release dist

version:
	@echo "VernisOS $(VERSION) (built $(BUILD_DATE))"

size-report: $(UNIVERSAL_IMG)
	@echo "=== VernisOS $(VERSION) Size Report ==="
	@echo "Build date: $(BUILD_DATE)"
	@echo ""
	@echo "Kernel binaries:"
	@printf "  x86 : %6d bytes (%d KB)\n" $$(wc -c < make/kernel/arch/x86/kernel_x86.bin) $$(( $$(wc -c < make/kernel/arch/x86/kernel_x86.bin) / 1024 ))
	@printf "  x64 : %6d bytes (%d KB)\n" $$(wc -c < make/kernel/arch/x86_64/kernel_x64.bin) $$(( $$(wc -c < make/kernel/arch/x86_64/kernel_x64.bin) / 1024 ))
	@echo ""
	@echo "Boot stages:"
	@printf "  stage1: %5d bytes\n" $$(wc -c < make/boot/x86/stage1.bin)
	@printf "  stage2: %5d bytes\n" $$(wc -c < make/boot/x86/stage2.bin)
	@printf "  stage3: %5d bytes\n" $$(wc -c < make/boot/x86/stage3.bin)
	@echo ""
	@echo "Data blobs:"
	@printf "  policy : %5d bytes\n" $$(wc -c < make/policy.bin)
	@printf "  vernisfs: %4d bytes\n" $$(wc -c < make/vernisfs.bin)
	@echo ""
	@printf "Disk image: %s\n" "$$(ls -lh os.img | awk '{print $$5}')"
	@echo ""
	@echo "Rust static libraries:"
	@printf "  x86 : %s\n" "$$(ls -lh lib/x86/libvernisos.a 2>/dev/null | awk '{print $$5}' || echo 'not built')"
	@printf "  x64 : %s\n" "$$(ls -lh lib/x86_64/libvernisos_x64.a 2>/dev/null | awk '{print $$5}' || echo 'not built')"

prerequisites:
	@echo "Checking build prerequisites..."
	@echo ""
	@printf "  %-25s " "i686-elf-gcc" && (which i686-elf-gcc > /dev/null 2>&1 && echo "OK ($$(i686-elf-gcc --version | head -1))" || echo "MISSING")
	@printf "  %-25s " "x86_64-elf-gcc" && (which x86_64-elf-gcc > /dev/null 2>&1 && echo "OK ($$(x86_64-elf-gcc --version | head -1))" || echo "MISSING")
	@printf "  %-25s " "nasm" && (which nasm > /dev/null 2>&1 && echo "OK ($$(nasm -v 2>&1 | head -1))" || echo "MISSING")
	@printf "  %-25s " "cargo (nightly)" && (cargo +nightly --version > /dev/null 2>&1 && echo "OK ($$(cargo +nightly --version))" || echo "MISSING")
	@printf "  %-25s " "qemu-system-x86_64" && (which qemu-system-x86_64 > /dev/null 2>&1 && echo "OK" || echo "MISSING (optional, for testing)")
	@printf "  %-25s " "python3" && (which python3 > /dev/null 2>&1 && echo "OK ($$(python3 --version))" || echo "MISSING (optional, for tests)")
	@printf "  %-25s " "i686-elf-ld" && (which i686-elf-ld > /dev/null 2>&1 && echo "OK" || echo "MISSING")
	@printf "  %-25s " "x86_64-elf-ld" && (which x86_64-elf-ld > /dev/null 2>&1 && echo "OK" || echo "MISSING")

release: clean $(UNIVERSAL_IMG) size-report
	@echo ""
	@echo "=== Release build complete: VernisOS $(VERSION) ==="

DIST_NAME = VernisOS-$(VERSION)
dist: release
	@echo "Packaging $(DIST_NAME)..."
	@mkdir -p dist/$(DIST_NAME)
	@cp os.img dist/$(DIST_NAME)/
	@cp VERSION dist/$(DIST_NAME)/
	@cp README.md dist/$(DIST_NAME)/
	@cp -r docs dist/$(DIST_NAME)/ 2>/dev/null || true
	@cp CHANGELOG.md dist/$(DIST_NAME)/ 2>/dev/null || true
	@cp GETTING_STARTED.md dist/$(DIST_NAME)/ 2>/dev/null || true
	@cd dist && tar czf $(DIST_NAME).tar.gz $(DIST_NAME)
	@echo "Created: dist/$(DIST_NAME).tar.gz"
	@ls -lh dist/$(DIST_NAME).tar.gz

# ==== Remove all ====
clean:
	rm -f $(STAGE1_BIN) $(STAGE2_BIN) $(STAGE3_BIN) \
	      $(KERNEL_X86_OBJ) $(KERNEL_X64_OBJ) \
	      $(KERNEL_X64_SHIM) $(KERNEL_X64_INTR) $(KERNEL_X64_SYSC) \
	      $(KERNEL_X64_IPC) $(KERNEL_X64_MOD) \
	      $(KERNEL_X86_IPC) $(KERNEL_X86_MOD) \
	      $(KERNEL_X86_AI) $(KERNEL_X64_AI) \
	      $(KERNEL_X86_ELF) $(KERNEL_X64_ELF) \
	      $(KERNEL_X86_BIN) $(KERNEL_X64_BIN) \
	      $(UNIVERSAL_IMG)
	clear

# ==== Help ====
help:
	@echo "================================================"
	@echo "  VernisOS $(VERSION) — Build System"
	@echo "================================================"
	@echo ""
	@echo "Build:"
	@echo "  make              Build all platforms"
	@echo "  make build-x86    Build x86 kernel only"
	@echo "  make build-x64    Build x64 kernel only"
	@echo "  make rust          Rebuild Rust libraries"
	@echo ""
	@echo "Run:"
	@echo "  make run32        Run x86 in QEMU"
	@echo "  make run64        Run x64 in QEMU"
	@echo "  make run64-ai     Run x64 with AI bridge (TCP 4444)"
	@echo ""
	@echo "Debug:"
	@echo "  make debug32      x86 + GDB stub (:1234)"
	@echo "  make debug64      x64 + GDB stub (:1234)"
	@echo ""
	@echo "Test:"
	@echo "  make test         Run integration tests (x64)"
	@echo "  make test-x86     Run integration tests (x86)"
	@echo "  make test-all     Run tests for both archs"
	@echo ""
	@echo "Release:"
	@echo "  make release      Clean build + size report"
	@echo "  make dist         Package release archive"
	@echo "  make size-report  Show binary sizes"
	@echo "  make version      Show version info"
	@echo "  make prerequisites Check toolchain"
	@echo ""
	@echo "Utility:"
	@echo "  make clean        Remove build artifacts"
	@echo "  make help         Show this help"

# ==== Compile tcp.c (x86) ====
KERNEL_X86_TCP = make/kernel/arch/x86/tcp.o
KERNEL_X64_TCP = make/kernel/arch/x86_64/tcp.o

$(KERNEL_X86_TCP): kernel/net/tcp.c include/tcp.h | prepare
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

$(KERNEL_X64_TCP): kernel/net/tcp.c include/tcp.h | prepare
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $@

