// policy_loader.c — Load VPOL policy blob from disk at boot
//
// Phase 12: Reads policy binary from os.img sector 2048 via ATA PIO
// and feeds it into the Rust AI engine.

#include "ai_bridge.h"
#include <stdint.h>
#include <stddef.h>

extern void serial_print(const char *s);
extern void serial_print_dec(uint32_t val);

// ATA PIO ports (primary bus)
#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_CMD        0x1F7
#define ATA_STATUS     0x1F7

#define ATA_CMD_READ   0x20
#define ATA_SR_BSY     0x80
#define ATA_SR_DRQ     0x08

// Policy location in os.img
#define POLICY_SECTOR  4096
#define POLICY_MAX_SECTORS 4   // max 2048 bytes (4 sectors)

// Port I/O
static inline void outb_ata(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb_ata(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint16_t inw_ata(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

// Wait for drive ready
static int ata_wait_ready(void) {
    uint32_t timeout = 1000000;
    while (timeout--) {
        uint8_t status = inb_ata(ATA_STATUS);
        if (!(status & ATA_SR_BSY)) {
            if (status & ATA_SR_DRQ) return 1;
            if (status & 0x01) return -1; // ERR bit
        }
    }
    return 0; // timeout
}

// Read sectors via ATA PIO (28-bit LBA)
static int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf) {
    // Select drive 0, LBA mode
    outb_ata(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb_ata(ATA_SECCOUNT, count);
    outb_ata(ATA_LBA_LO, lba & 0xFF);
    outb_ata(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb_ata(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb_ata(ATA_CMD, ATA_CMD_READ);

    for (uint8_t s = 0; s < count; s++) {
        int ready = ata_wait_ready();
        if (ready <= 0) return -1;

        // Read 256 words (512 bytes) per sector
        uint16_t *dst = (uint16_t*)(buf + s * 512);
        for (int i = 0; i < 256; i++) {
            dst[i] = inw_ata(ATA_DATA);
        }
    }
    return count;
}

// Static buffer for policy blob (max 4 sectors = 2KB)
static uint8_t policy_buf[POLICY_MAX_SECTORS * 512];

void policy_load_from_disk(void) {
    serial_print("[policy] Loading from sector ");
    serial_print_dec(POLICY_SECTOR);
    serial_print("...\n");

    int result = ata_read_sectors(POLICY_SECTOR, POLICY_MAX_SECTORS, policy_buf);
    if (result < 0) {
        serial_print("[policy] ATA read failed\n");
        return;
    }

    // Check VPOL magic
    if (policy_buf[0] != 'V' || policy_buf[1] != 'P' ||
        policy_buf[2] != 'O' || policy_buf[3] != 'L') {
        serial_print("[policy] No policy blob found (missing VPOL magic)\n");
        return;
    }

    // Determine actual blob size from section headers
    uint16_t section_count = policy_buf[6] | (policy_buf[7] << 8);
    size_t offset = 8; // after header

    for (uint16_t i = 0; i < section_count; i++) {
        if (offset + 8 > sizeof(policy_buf)) break;
        uint32_t data_size = policy_buf[offset + 4] |
                            (policy_buf[offset + 5] << 8) |
                            (policy_buf[offset + 6] << 16) |
                            (policy_buf[offset + 7] << 24);
        offset += 8 + data_size;
    }

    serial_print("[policy] Blob size: ");
    serial_print_dec((uint32_t)offset);
    serial_print(" bytes, ");
    serial_print_dec(section_count);
    serial_print(" sections\n");

    // Feed to AI engine
    uint32_t ok = ai_kernel_engine_load_policy(policy_buf, offset);
    if (ok) {
        serial_print("[policy] Loaded OK, version ");
        serial_print_dec(ai_kernel_engine_policy_version());
        serial_print("\n");
    } else {
        serial_print("[policy] Load FAILED\n");
    }
}
