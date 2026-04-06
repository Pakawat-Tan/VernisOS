#!/usr/bin/env python3
"""mkfs_vernis.py — Create a VernisFS filesystem image.

Phase 13: Generates a binary filesystem image with superblock, file table,
and default files (/etc/shadow with default users).

Usage:
    python3 ai/tools/mkfs_vernis.py -o make/vernisfs.bin
"""

import argparse
import hashlib
import os
import struct
import sys

# Constants matching vernisfs.h
VFS_MAGIC = 0x53465600  # "VFS\0" little-endian
VFS_VERSION = 1
VFS_MAX_FILES = 32
VFS_MAX_FILENAME = 32
VFS_FILETABLE_SECTORS = 4
VFS_DATA_SECTOR = 5  # Relative sector where data starts

# File entry: 64 bytes
FILE_ENTRY_SIZE = 64

# User record: 66 bytes (matching userdb.h)
USER_RECORD_SIZE = 66
USER_FLAG_ACTIVE = 0x01
USER_FLAG_NO_PASSWORD = 0x04


# ---- ELF binary generators (Phase 19) ----

def make_elf64_hello() -> bytes:
    """Generate a minimal ELF64 executable that loops calling int 0x80 with eax=0xF0."""
    # User program machine code (x86_64):
    #   mov rax, 0xF0   ; SYS_USER_TEST
    #   int 0x80
    #   jmp -11          ; loop back
    code = bytes([
        0x48, 0xC7, 0xC0, 0xF0, 0x00, 0x00, 0x00,  # mov rax, 0xF0
        0xCD, 0x80,                                    # int 0x80
        0xEB, 0xF5,                                    # jmp .-11
    ])

    vaddr = 0x10000000  # Must match USER_CODE_VADDR in kernel

    # ELF64 header (64 bytes)
    ehdr = struct.pack("<4sBBBBBxxxxxxx"  # e_ident (16 bytes)
                       "HH"               # e_type, e_machine
                       "I"                 # e_version
                       "QQQ"              # e_entry, e_phoff, e_shoff
                       "I"                 # e_flags
                       "HHH"              # e_ehsize, e_phentsize, e_phnum
                       "HHH",             # e_shentsize, e_shnum, e_shstrndx
                       b'\x7fELF',         # magic
                       2,                  # ELFCLASS64
                       1,                  # ELFDATA2LSB
                       1,                  # EV_CURRENT
                       0,                  # ELFOSABI_NONE
                       0,                  # padding
                       2,                  # ET_EXEC
                       0x3E,               # EM_X86_64
                       1,                  # EV_CURRENT
                       vaddr,              # e_entry
                       64,                 # e_phoff (right after ehdr)
                       0,                  # e_shoff (none)
                       0,                  # e_flags
                       64,                 # e_ehsize
                       56,                 # e_phentsize
                       1,                  # e_phnum
                       0, 0, 0)            # e_sh*

    # Program header (56 bytes) — single PT_LOAD
    phdr = struct.pack("<II"   # p_type, p_flags
                       "QQQ"   # p_offset, p_vaddr, p_paddr
                       "QQ"    # p_filesz, p_memsz
                       "Q",    # p_align
                       1,                  # PT_LOAD
                       5,                  # PF_R | PF_X
                       64 + 56,            # p_offset (code after ehdr+phdr)
                       vaddr,              # p_vaddr
                       vaddr,              # p_paddr
                       len(code),          # p_filesz
                       len(code),          # p_memsz
                       0x1000)             # p_align

    return ehdr + phdr + code


def make_elf32_hello() -> bytes:
    """Generate a minimal ELF32 executable that loops calling int 0x80 with eax=0xF0."""
    # User program machine code (i386):
    #   mov eax, 0xF0   ; SYS_USER_TEST
    #   int 0x80
    #   jmp -9           ; loop back
    code = bytes([
        0xB8, 0xF0, 0x00, 0x00, 0x00,  # mov eax, 0xF0
        0xCD, 0x80,                      # int 0x80
        0xEB, 0xF7,                      # jmp .-9
    ])

    vaddr = 0x10000000  # Must match USER_CODE_VADDR_32 in kernel

    # ELF32 header (52 bytes)
    ehdr = struct.pack("<4sBBBBBxxxxxxx"  # e_ident (16 bytes)
                       "HH"               # e_type, e_machine
                       "I"                 # e_version
                       "III"              # e_entry, e_phoff, e_shoff
                       "I"                 # e_flags
                       "HHH"              # e_ehsize, e_phentsize, e_phnum
                       "HHH",             # e_shentsize, e_shnum, e_shstrndx
                       b'\x7fELF',         # magic
                       1,                  # ELFCLASS32
                       1,                  # ELFDATA2LSB
                       1,                  # EV_CURRENT
                       0,                  # ELFOSABI_NONE
                       0,                  # padding
                       2,                  # ET_EXEC
                       3,                  # EM_386
                       1,                  # EV_CURRENT
                       vaddr,              # e_entry
                       52,                 # e_phoff (right after ehdr)
                       0,                  # e_shoff
                       0,                  # e_flags
                       52,                 # e_ehsize
                       32,                 # e_phentsize
                       1,                  # e_phnum
                       0, 0, 0)            # e_sh*

    # Program header (32 bytes) — single PT_LOAD
    phdr = struct.pack("<IIII"   # p_type, p_offset, p_vaddr, p_paddr
                       "III"     # p_filesz, p_memsz, p_flags
                       "I",      # p_align
                       1,                  # PT_LOAD
                       52 + 32,            # p_offset (code after ehdr+phdr)
                       vaddr,              # p_vaddr
                       vaddr,              # p_paddr
                       len(code),          # p_filesz
                       len(code),          # p_memsz
                       5,                  # PF_R | PF_X
                       0x1000)             # p_align

    return ehdr + phdr + code


def make_elf64_exit() -> bytes:
    """Generate a minimal ELF64 executable that sends a few heartbeats then calls SYS_EXIT(42)."""
    # User program machine code (x86_64):
    #   mov rbx, 5       ; counter
    # loop:
    #   mov rax, 0xF0    ; SYS_USER_TEST
    #   int 0x80
    #   dec rbx
    #   jnz loop
    #   mov rax, 60      ; SYS_EXIT
    #   mov rbx, 42      ; exit code
    #   int 0x80
    code = bytes([
        0x48, 0xC7, 0xC3, 0x05, 0x00, 0x00, 0x00,  # mov rbx, 5
        # loop:
        0x48, 0xC7, 0xC0, 0xF0, 0x00, 0x00, 0x00,  # mov rax, 0xF0
        0xCD, 0x80,                                    # int 0x80
        0x48, 0xFF, 0xCB,                              # dec rbx
        0x75, 0xF2,                                    # jnz loop (-14)
        0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00,  # mov rax, 60
        0x48, 0xC7, 0xC3, 0x2A, 0x00, 0x00, 0x00,  # mov rbx, 42
        0xCD, 0x80,                                    # int 0x80
    ])

    vaddr = 0x10000000

    ehdr = struct.pack("<4sBBBBBxxxxxxx"
                       "HH" "I" "QQQ" "I" "HHH" "HHH",
                       b'\x7fELF', 2, 1, 1, 0, 0,
                       2, 0x3E, 1,
                       vaddr, 64, 0, 0,
                       64, 56, 1, 0, 0, 0)

    phdr = struct.pack("<II" "QQQ" "QQ" "Q",
                       1, 5, 64 + 56, vaddr, vaddr,
                       len(code), len(code), 0x1000)

    return ehdr + phdr + code


def make_elf32_exit() -> bytes:
    """Generate a minimal ELF32 executable that sends a few heartbeats then calls SYS_EXIT(42)."""
    # User program machine code (i386):
    #   mov ecx, 5        ; counter
    # loop:
    #   mov eax, 0xF0     ; SYS_USER_TEST
    #   int 0x80
    #   dec ecx
    #   jnz loop
    #   mov eax, 60       ; SYS_EXIT
    #   mov ebx, 42       ; exit code
    #   int 0x80
    code = bytes([
        0xB9, 0x05, 0x00, 0x00, 0x00,  # mov ecx, 5
        # loop:
        0xB8, 0xF0, 0x00, 0x00, 0x00,  # mov eax, 0xF0
        0xCD, 0x80,                      # int 0x80
        0x49,                            # dec ecx
        0x75, 0xF6,                      # jnz loop (-10)
        0xB8, 0x3C, 0x00, 0x00, 0x00,  # mov eax, 60 (SYS_EXIT)
        0xBB, 0x2A, 0x00, 0x00, 0x00,  # mov ebx, 42 (exit code)
        0xCD, 0x80,                      # int 0x80
    ])

    vaddr = 0x10000000

    ehdr = struct.pack("<4sBBBBBxxxxxxx"
                       "HH" "I" "III" "I" "HHH" "HHH",
                       b'\x7fELF', 1, 1, 1, 0, 0,
                       2, 3, 1,
                       vaddr, 52, 0, 0,
                       52, 32, 1, 0, 0, 0)

    phdr = struct.pack("<IIII" "III" "I",
                       1, 52 + 32, vaddr, vaddr,
                       len(code), len(code), 5, 0x1000)

    return ehdr + phdr + code


def sha256_hash(password: str) -> bytes:
    """Hash a password with SHA-256."""
    return hashlib.sha256(password.encode("utf-8")).digest()


def make_user_record(username: str, password: str | None, privilege: int, flags: int) -> bytes:
    """Create a 66-byte user record."""
    name_bytes = username.encode("utf-8")[:VFS_MAX_FILENAME]
    name_padded = name_bytes.ljust(VFS_MAX_FILENAME, b"\x00")

    if password is not None:
        pw_hash = sha256_hash(password)
    else:
        pw_hash = b"\x00" * 32

    return struct.pack(
        f"{VFS_MAX_FILENAME}s32sBB",
        name_padded,
        pw_hash,
        privilege,
        flags,
    )


def make_file_entry(
    filename: str, start_sector: int, size: int, ftype: int = 1, flags: int = 0
) -> bytes:
    """Create a 64-byte file table entry."""
    name_bytes = filename.encode("utf-8")[:VFS_MAX_FILENAME]
    name_padded = name_bytes.ljust(VFS_MAX_FILENAME, b"\x00")

    entry = struct.pack(
        f"{VFS_MAX_FILENAME}sIIBB",
        name_padded,
        start_sector,
        size,
        ftype,
        flags,
    )
    # Pad to 64 bytes
    return entry.ljust(FILE_ENTRY_SIZE, b"\x00")


def make_superblock(file_count: int, first_free_sector: int) -> bytes:
    """Create a 512-byte superblock."""
    sb = struct.pack(
        "<IHHII",
        VFS_MAGIC,
        VFS_VERSION,
        file_count,
        1024,  # total_data_sectors (generous)
        first_free_sector,
    )
    # Pad to 512 bytes
    return sb.ljust(512, b"\x00")


def main():
    parser = argparse.ArgumentParser(description="Create VernisFS image")
    parser.add_argument("-o", "--output", default="make/vernisfs.bin", help="Output file")
    parser.add_argument("--vsh64", default="make/user/vsh64.elf", help="Optional x86_64 vsh ELF path")
    parser.add_argument("--vsh32", default="make/user/vsh32.elf", help="Optional i686 vsh ELF path")
    args = parser.parse_args()

    # === Build default files ===

    # /etc/shadow — user database
    users = [
        make_user_record("root", None, 0, USER_FLAG_ACTIVE | USER_FLAG_NO_PASSWORD),
        make_user_record("admin", "admin", 50, USER_FLAG_ACTIVE),
        make_user_record("user", "user", 100, USER_FLAG_ACTIVE),
    ]
    shadow_data = b"".join(users)

    # /etc/passwd — human-readable user info
    passwd_lines = "root:x:0:root\nadmin:x:50:admin\nuser:x:100:user\n"
    passwd_data = passwd_lines.encode("utf-8")

    # === Assign data sectors ===
    files = []
    data_blocks = []
    next_sector = 0

    # File 1: /etc/shadow
    shadow_sectors = (len(shadow_data) + 511) // 512
    files.append(("/etc/shadow", next_sector, len(shadow_data), 1, 0x02))  # system flag
    data_blocks.append(shadow_data)
    next_sector += shadow_sectors

    # File 2: /etc/passwd
    passwd_sectors = (len(passwd_data) + 511) // 512
    files.append(("/etc/passwd", next_sector, len(passwd_data), 1, 0x01))  # readonly
    data_blocks.append(passwd_data)
    next_sector += passwd_sectors

    # File 3: /bin/hello64 — ELF64 test program (Phase 19)
    hello64_data = make_elf64_hello()
    hello64_sectors = (len(hello64_data) + 511) // 512
    files.append(("/bin/hello64", next_sector, len(hello64_data), 1, 0x00))
    data_blocks.append(hello64_data)
    next_sector += hello64_sectors

    # File 4: /bin/hello32 — ELF32 test program (Phase 19)
    hello32_data = make_elf32_hello()
    hello32_sectors = (len(hello32_data) + 511) // 512
    files.append(("/bin/hello32", next_sector, len(hello32_data), 1, 0x00))
    data_blocks.append(hello32_data)
    next_sector += hello32_sectors

    # File 5: /bin/exit64 — ELF64 program that calls SYS_EXIT (Phase 20)
    exit64_data = make_elf64_exit()
    exit64_sectors = (len(exit64_data) + 511) // 512
    files.append(("/bin/exit64", next_sector, len(exit64_data), 1, 0x00))
    data_blocks.append(exit64_data)
    next_sector += exit64_sectors

    # File 6: /bin/exit32 — ELF32 program that calls SYS_EXIT (Phase 20)
    exit32_data = make_elf32_exit()
    exit32_sectors = (len(exit32_data) + 511) // 512
    files.append(("/bin/exit32", next_sector, len(exit32_data), 1, 0x00))
    data_blocks.append(exit32_data)
    next_sector += exit32_sectors

    # File 7/8: /bin/vsh64 + /bin/vsh32 (Phase 45, optional external build)
    if args.vsh64 and os.path.exists(args.vsh64):
        with open(args.vsh64, "rb") as f:
            vsh64_data = f.read()
        if vsh64_data:
            vsh64_sectors = (len(vsh64_data) + 511) // 512
            files.append(("/bin/vsh64", next_sector, len(vsh64_data), 1, 0x00))
            data_blocks.append(vsh64_data)
            next_sector += vsh64_sectors

    if args.vsh32 and os.path.exists(args.vsh32):
        with open(args.vsh32, "rb") as f:
            vsh32_data = f.read()
        if vsh32_data:
            vsh32_sectors = (len(vsh32_data) + 511) // 512
            files.append(("/bin/vsh32", next_sector, len(vsh32_data), 1, 0x00))
            data_blocks.append(vsh32_data)
            next_sector += vsh32_sectors

    # === Build image ===

    # Superblock
    sb = make_superblock(file_count=len(files), first_free_sector=next_sector)

    # File table (VFS_FILETABLE_SECTORS * 512 bytes)
    ft = b""
    for fname, start, size, ftype, flags in files:
        ft += make_file_entry(fname, start, size, ftype, flags)
    # Pad remaining entries to fill 4 sectors
    ft = ft.ljust(VFS_FILETABLE_SECTORS * 512, b"\x00")

    # Data blocks
    data = b""
    for block in data_blocks:
        # Pad each file's data to sector boundary
        padded = block.ljust(((len(block) + 511) // 512) * 512, b"\x00")
        data += padded

    # Assemble image
    image = sb + ft + data

    with open(args.output, "wb") as f:
        f.write(image)

    print(f"VernisFS image: {len(image)} bytes ({len(image) // 512} sectors)")
    print(f"  Files: {len(files)}")
    for fname, start, size, ftype, flags in files:
        print(f"    {fname}: sector {start}, {size} bytes")
    print(f"  Output: {args.output}")


if __name__ == "__main__":
    main()
