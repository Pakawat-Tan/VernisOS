# Boot stages
STAGE1 = boot/CISC/boot_stage1.asm
STAGE2 = boot/CISC/boot_stage2.asm
STAGE3 = boot/CISC/boot_stage3.asm

# Kernel sources
KERNEL_X86_SRC = kernel/arch/x86/kernel_x86.c
KERNEL_X64_SRC = kernel/arch/x86_64/kernel_x64.c

# Core library (ไม่ใช้แล้ว)
CORE_LIB_DIR = kernel/core/verniskernel
CORE_LIB = $(CORE_LIB_DIR)


# VernisOS Rust staticlib
VERNISOS_DIR = lib/a
VERNISOS_LIB = $(VERNISOS_DIR)/x86/libvernisos.a
VERNISOS_INC = -I lib/h

# Kernel linker
KERNEL_X86_LD = kernel/arch/x86/linker.ld
KERNEL_X64_LD = kernel/arch/x86_64/linker.ld

# Boot output
STAGE1_BIN = make/boot/CISC/boot_stage1.bin
STAGE2_BIN = make/boot/CISC/boot_stage2.bin
STAGE3_BIN = make/boot/CISC/boot_stage3.bin

# Kernel output
KERNEL_X86_SHIM = make/kernel/arch/x86/rust_shims.o
KERNEL_X86_OBJ = make/kernel/arch/x86/kernel_x86.o $(KERNEL_X86_SHIM)
KERNEL_X64_OBJ = make/kernel/arch/x86_64/kernel_x64.o

KERNEL_X86_ELF = make/kernel/arch/x86/kernel_x86.elf
KERNEL_X64_ELF = make/kernel/arch/x86_64/kernel_x64.elf

KERNEL_X86_BIN = make/kernel/arch/x86/kernel_x86.bin
KERNEL_X64_BIN = make/kernel/arch/x86_64/kernel_x64.bin

UNIVERSAL_IMG = os.img

# ==== Toolchains ====
CC_X86 = i686-elf-gcc
CC_X64 = x86_64-elf-gcc

LD_X86 = i686-elf-ld
LD_X64 = x86_64-elf-ld

OBJCOPY_X86 = i686-elf-objcopy
OBJCOPY_X64 = x86_64-elf-objcopy

# Rust target for core library
RUST_TARGET = i386.json

# ==== Flags ====
CFLAGS = -Wall -Wextra -O2 -std=gnu99 -ffreestanding -fno-pie
CFLAGS_X86 = $(CFLAGS) -m32 -march=i686 -fno-omit-frame-pointer -g
CFLAGS_X64 = $(CFLAGS) -mno-red-zone -mcmodel=kernel -fno-omit-frame-pointer -g
CFLAGS_ARM64 = $(CFLAGS) -march=armv8-a -g

.PHONY: all clean run build_script \
        build-x86 build-x64 build-arm64 \
        run32 run64 run-arm64 debug show prepare help \
        build-core

PLATFORM ?= all

all: prepare build-core $(UNIVERSAL_IMG) build_script

# ==== Prepare directories ====
prepare:
	mkdir -p make/boot
	mkdir -p make/boot/CISC
	mkdir -p make/boot/RISC
	mkdir -p make/kernel
	mkdir -p make/kernel/arch
	mkdir -p make/kernel/arch/x86
	mkdir -p make/kernel/arch/x86_64

# ==== Build core library ====
build-core: $(VERNISOS_LIB)

$(VERNISOS_LIB):
	@echo "Building core library..."
	cd $(CORE_LIB_DIR) && cargo build -Zbuild-std=core,alloc --target $(RUST_TARGET) --release
	cp $(CORE_LIB_DIR)/target/i386/release/libvernisos.a $(VERNISOS_DIR)/x86/libvernisos.a
	ranlib $@

# ==== Kernel Build Targets ====
build-x86: build-core $(KERNEL_X86_BIN)
build-x64: build-core $(KERNEL_X64_BIN)

build-kernels:
ifeq ($(PLATFORM),all)
	$(MAKE) build-x86
	$(MAKE) build-x64
else ifeq ($(PLATFORM),x86)
	$(MAKE) build-x86
else ifeq ($(PLATFORM),x64)
	$(MAKE) build-x64
endif

# ==== Universal Image ====
$(UNIVERSAL_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(STAGE3_BIN) \
                  $(KERNEL_X86_BIN) $(KERNEL_X64_BIN)

	@echo "Creating universal image..."
	rm -f $@
	dd if=/dev/zero of=$@ bs=1M count=4 status=none

	@echo "Adding x86 bootloader..."
	dd if=$(STAGE1_BIN) of=$@ bs=512 count=1 conv=notrunc
	dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc
	dd if=$(STAGE3_BIN) of=$@ bs=512 seek=6 conv=notrunc

	@echo "Adding x86 kernel..."
	dd if=$(KERNEL_X86_BIN) of=$@ bs=512 seek=12 conv=notrunc

	@echo "Adding x64 kernel..."
	dd if=$(KERNEL_X64_BIN) of=$@ bs=512 seek=30 conv=notrunc


	@echo "Done: $(UNIVERSAL_IMG)"
	ls -lh $@

# ==== Boot stages ====
$(STAGE1_BIN): $(STAGE1)
	nasm -f bin -o $@ $<

$(STAGE2_BIN): $(STAGE2)
	nasm -f bin -o $@ $<
	truncate -s 2048 $@

$(STAGE3_BIN): $(STAGE3)
	nasm -f bin -o $@ $<
	truncate -s 2048 $@

# ==== Compile kernel_x86.c เป็น .o ====
make/kernel/arch/x86/kernel_x86.o: kernel/arch/x86/kernel_x86.c
	$(CC_X86) $(CFLAGS_X86) $(VERNISOS_INC) -c $< -o $@

# ==== Compile rust_shims.c เป็น .o ====
make/kernel/arch/x86/rust_shims.o: lib/c/rust_shims.c
	$(CC_X86) $(CFLAGS_X86) -c $< -o $@

# ==== Kernel compilation (link รวม .o ทั้งหมด) ====
$(KERNEL_X86_BIN): $(KERNEL_X86_OBJ) $(KERNEL_X86_LD) $(VERNISOS_LIB)
	$(LD_X86) -T $(KERNEL_X86_LD) -m elf_i386 $(KERNEL_X86_OBJ) $(VERNISOS_LIB) -o $(KERNEL_X86_ELF)
	$(OBJCOPY_X86) -O binary $(KERNEL_X86_ELF) $@

$(KERNEL_X64_BIN): $(KERNEL_X64_SRC) $(KERNEL_X64_LD) $(VERNISOS_LIB)
	$(CC_X64) $(CFLAGS_X64) $(VERNISOS_INC) -c $< -o $(KERNEL_X64_OBJ)
	$(LD_X64) -T $(KERNEL_X64_LD) -m elf_x86_64 $(KERNEL_X64_OBJ) $(VERNISOS_LIB) -o $(KERNEL_X64_ELF)
	$(OBJCOPY_X64) -O binary $(KERNEL_X64_ELF) $@

# ==== Run/Debug ====
run32: $(UNIVERSAL_IMG)
	qemu-system-i386 -drive format=raw,file=$(UNIVERSAL_IMG)

run64: $(UNIVERSAL_IMG)
	qemu-system-x86_64 -drive format=raw,file=$(UNIVERSAL_IMG)


debug32: $(UNIVERSAL_IMG)
	@echo "Use GDB with: target remote :1234"
	qemu-system-i386 -s -S -drive format=raw,file=$(UNIVERSAL_IMG)

debug64: $(UNIVERSAL_IMG)
	@echo "Use GDB with: target remote :1234"
	qemu-system-x86_64 -s -S -drive format=raw,file=$(UNIVERSAL_IMG)


# ==== Remove all ====
clean:
	rm -f $(STAGE1_BIN) $(STAGE2_BIN) $(STAGE3_BIN) \
	      $(KERNEL_X86_OBJ) $(KERNEL_X64_OBJ) \
	      $(KERNEL_X86_ELF) $(KERNEL_X64_ELF) \
	      $(KERNEL_X86_BIN) $(KERNEL_X64_BIN) \
	      $(UNIVERSAL_IMG)
	clear

# ==== Help ====
help:
	clear
	@echo "================================================"
	@echo "                 Make Help                      "
	@echo "================================================"
	@echo "Usage: make [target]"
	@echo "Targets:"
	@echo "  all             - Build all platforms"
	@echo "  build-core      - Build core library"
	@echo "  build-x86       - Build x86 kernel"
	@echo "  build-x64       - Build x64 kernel"
	@echo "  build-kernels   - Build all kernels (can set PLATFORM=x86|x64)"
	@echo ""
	@echo "Run targets:"
	@echo "  run32           - Run x86 kernel"
	@echo "  run64           - Run x64 kernel"
	@echo ""
	@echo "Debug targets:"
	@echo "  debug32         - Debug x86 kernel"
	@echo "  debug64         - Debug x64 kernel"
	@echo ""
	@echo "Other:"
	@echo "  clean           - Clean all build files"
	@echo "  help            - Show this help"
	@echo "================================================"
