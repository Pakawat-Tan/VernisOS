# Boot stages
STAGE1 = boot/CISC/boot_stage1.asm
STAGE2 = boot/CISC/boot_stage2.asm
STAGE3 = boot/CISC/boot_stage3.asm
BOOT_ARM = boot/RISC/boot_arm64.s

# Kernel sources
KERNEL_X86_SRC = kernel/arch/x86/kernel_x86.c
KERNEL_X64_SRC = kernel/arch/x86_64/kernel_x64.c
KERNEL_ARM64_SRC = kernel/arch/arm64/kernel_arm64.c

# Kernel linker
KERNEL_X86_LD = kernel/arch/x86/linker.ld
KERNEL_X64_LD = kernel/arch/x86_64/linker.ld
KERNEL_ARM64_LD = kernel/arch/arm64/linker.ld

# Boot output
STAGE1_BIN = make/boot/CISC/boot_stage1.bin
STAGE2_BIN = make/boot/CISC/boot_stage2.bin
STAGE3_BIN = make/boot/CISC/boot_stage3.bin
BOOT_ARM_O = make/boot/RISC/boot_arm.o
BOOT_ARM_ELF = make/boot/RISC/boot_arm.elf
BOOT_ARM_BIN = make/boot/RISC/boot_arm.bin

# Kernel output
KERNEL_X86_OBJ = make/kernel/arch/x86/kernel_x86.o
KERNEL_X64_OBJ = make/kernel/arch/x86_64/kernel_x64.o
KERNEL_ARM64_OBJ = make/kernel/arch/arm64/kernel_arm64.o

KERNEL_X86_ELF = make/kernel_x86.elf
KERNEL_X64_ELF = make/kernel_x64.elf
KERNEL_ARM64_ELF = make/kernel_arm64.elf

KERNEL_X86_BIN = make/kernel/arch/x86/kernel_x86.bin
KERNEL_X64_BIN = make/kernel/arch/x86_64/kernel_x64.bin
KERNEL_ARM64_BIN = make/kernel/arch/arm64/kernel_arm64.bin

UNIVERSAL_IMG = os.img

# ==== Toolchains ====
CC_X86 = i686-elf-gcc
CC_X64 = x86_64-elf-gcc
CC_ARM64 = aarch64-none-elf-gcc

LD_X86 = i686-elf-ld
LD_X64 = x86_64-elf-ld
LD_ARM64 = aarch64-none-elf-ld

OBJCOPY_X86 = i686-elf-objcopy
OBJCOPY_X64 = x86_64-elf-objcopy
OBJCOPY_ARM64 = aarch64-none-elf-objcopy

AS_ARM64 = aarch64-none-elf-as

# ==== Flags ====
CFLAGS = -Wall -Wextra -O2 -std=gnu99 -ffreestanding -fno-pie
CFLAGS_X86 = $(CFLAGS) -m32 -march=i686 -fno-omit-frame-pointer -g
CFLAGS_X64 = $(CFLAGS) -mno-red-zone -mcmodel=kernel -fno-omit-frame-pointer -g
CFLAGS_ARM64 = $(CFLAGS) -march=armv8-a -g

.PHONY: all clean run build_script \
        build-x86 build-x64 build-arm64 \
        run32 run64 run-arm64 debug show prepare help

PLATFORM ?= all

all: prepare $(UNIVERSAL_IMG) build_script

# ==== Prepare directories ====
prepare:
	mkdir -p make/boot
	mkdir -p make/boot/CISC
	mkdir -p make/boot/RISC
	mkdir -p make/kernel
	mkdir -p make/kernel/arch
	mkdir -p make/kernel/arch/x86
	mkdir -p make/kernel/arch/x86_64
	mkdir -p make/kernel/arch/arm64


# ==== Kernel Build Targets ====
build-x86: $(KERNEL_X86_BIN)
build-x64: $(KERNEL_X64_BIN)
build-arm64: $(KERNEL_ARM64_BIN)

build-kernels:
ifeq ($(PLATFORM),all)
	$(MAKE) build-x86
	$(MAKE) build-x64
	$(MAKE) build-arm64
else ifeq ($(PLATFORM),x86)
	$(MAKE) build-x86
else ifeq ($(PLATFORM),x64)
	$(MAKE) build-x64
else ifeq ($(PLATFORM),arm64)
	$(MAKE) build-arm64
endif

# ==== Universal Image ====
$(UNIVERSAL_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(STAGE3_BIN) $(BOOT_ARM_BIN) \
                  $(KERNEL_X86_BIN) $(KERNEL_X64_BIN) $(KERNEL_ARM64_BIN)

	@echo "Creating universal image..."
	rm -f $@
	dd if=/dev/zero of=$@ bs=1M count=4 status=none

	@echo "Adding x86 bootloader..."
	dd if=$(STAGE1_BIN) of=$@ bs=512 count=1 conv=notrunc
	dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc
	dd if=$(STAGE3_BIN) of=$@ bs=512 seek=6 conv=notrunc
	dd if=$(BOOT_ARM_BIN) of=$@ bs=512 seek=48 conv=notrunc

	@echo "Adding x86 kernel..."
	dd if=$(KERNEL_X86_BIN) of=$@ bs=512 seek=12 conv=notrunc

	@echo "Adding x64 kernel..."
	dd if=$(KERNEL_X64_BIN) of=$@ bs=512 seek=30 conv=notrunc

	@echo "Adding ARM64 kernel..."
	dd if=$(KERNEL_ARM64_BIN) of=$@ bs=512 seek=54 conv=notrunc

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

$(BOOT_ARM_BIN): $(BOOT_ARM)
	$(AS_ARM64) -o $(BOOT_ARM_O) $<
	$(LD_ARM64) -Ttext=0x40000000 -o $(BOOT_ARM_ELF) $(BOOT_ARM_O)
	$(OBJCOPY_ARM64) -O binary $(BOOT_ARM_ELF) $@

# ==== Kernel compilation ====
$(KERNEL_X86_BIN): $(KERNEL_X86_SRC) $(KERNEL_X86_LD)
	$(CC_X86) $(CFLAGS_X86) -c $< -o $(KERNEL_X86_OBJ)
	$(LD_X86) -T $(KERNEL_X86_LD) -m elf_i386 $(KERNEL_X86_OBJ) -o $(KERNEL_X86_ELF)
	$(OBJCOPY_X86) -O binary $(KERNEL_X86_ELF) $@

$(KERNEL_X64_BIN): $(KERNEL_X64_SRC) $(KERNEL_X64_LD)
	$(CC_X64) $(CFLAGS_X64) -c $< -o $(KERNEL_X64_OBJ)
	$(LD_X64) -T $(KERNEL_X64_LD) -m elf_x86_64 $(KERNEL_X64_OBJ) -o $(KERNEL_X64_ELF)
	$(OBJCOPY_X64) -O binary $(KERNEL_X64_ELF) $@

$(KERNEL_ARM64_BIN): $(KERNEL_ARM64_SRC) $(KERNEL_ARM64_LD)
	$(CC_ARM64) $(CFLAGS_ARM64) -c $< -o $(KERNEL_ARM64_OBJ)
	$(LD_ARM64) -T $(KERNEL_ARM64_LD) $(KERNEL_ARM64_OBJ) -o $(KERNEL_ARM64_ELF)
	$(OBJCOPY_ARM64) -O binary $(KERNEL_ARM64_ELF) $@

# ==== Run/Debug ====
run32: $(UNIVERSAL_IMG)
	qemu-system-i386 -drive format=raw,file=$(UNIVERSAL_IMG)

run64: $(UNIVERSAL_IMG)
	qemu-system-x86_64 -drive format=raw,file=$(UNIVERSAL_IMG)

run-arm64: $(UNIVERSAL_IMG)
	qemu-system-aarch64 -M virt -cpu cortex-a53 -m 512M -drive format=raw,file=$(UNIVERSAL_IMG)

debug32: $(UNIVERSAL_IMG)
	@echo "Use GDB with: target remote :1234"
	qemu-system-i386 -s -S -drive format=raw,file=$(UNIVERSAL_IMG)

debug64: $(UNIVERSAL_IMG)
	@echo "Use GDB with: target remote :1234"
	qemu-system-x86_64 -s -S -drive format=raw,file=$(UNIVERSAL_IMG)

debug-arm64: $(UNIVERSAL_IMG)
	@echo "Use GDB with: target remote :1234"
	qemu-system-aarch64 -M virt -cpu cortex-a53 -s -S -drive format=raw,file=$(UNIVERSAL_IMG)

# ==== Remove all ====
clean:
	rm -f $(STAGE1_BIN) $(STAGE2_BIN) $(STAGE3_BIN) $(BOOT_ARM_BIN) \
	      $(KERNEL_X86_OBJ) $(KERNEL_X64_OBJ) $(KERNEL_ARM64_OBJ) \
	      $(KERNEL_X86_ELF) $(KERNEL_X64_ELF) $(KERNEL_ARM64_ELF) \
	      $(KERNEL_X86_BIN) $(KERNEL_X64_BIN) $(KERNEL_ARM64_BIN) \
	      $(UNIVERSAL_IMG) boot_arm.o boot_arm.elf

# ==== Help ====
help:
	@echo "================================================"
	@echo "                 Make Help                      "
	@echo "================================================"
	@echo "Usage: make [target]"
	@echo "Targets:"
	@echo "  all             - Build all platforms"
	@echo "  build-x86       - Build x86 kernel"
	@echo "  build-x64       - Build x64 kernel"
	@echo "  build-arm64     - Build ARM64 kernel"
	@echo "  build-kernels   - Build all kernels (can set PLATFORM=x86|x64|arm64)"
	@echo "  run32           - Run x86 kernel"
	@echo "  run64           - Run x64 kernel"
	@echo "  run-arm64       - Run ARM64 kernel"
	@echo "  debug32         - Debug x86 kernel"
	@echo "  debug64         - Debug x64 kernel"
	@echo "  debug-arm64     - Debug ARM64 kernel"
	@echo "  clean           - Clean all build files"
	@echo "  help            - Show this help"
	@echo "================================================"
