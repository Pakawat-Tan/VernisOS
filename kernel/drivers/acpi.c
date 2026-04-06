// acpi.c — minimal ACPI discovery for power control

#include "acpi.h"
#include <stddef.h>
#include <stdint.h>

extern void serial_print(const char *s);

static inline void outb_acpi(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline void outw_acpi(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port));
}

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) RsdpDescriptor;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) AcpiSdtHeader;

typedef struct {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed)) AcpiGas;

static uint16_t g_pm1a_cnt = 0;
static uint16_t g_pm1b_cnt = 0;
static uint16_t g_slp_typa = 0;
static uint16_t g_slp_typb = 0;
static uint8_t g_reset_value = 0;
static AcpiGas g_reset_reg;
static uint8_t g_acpi_ready = 0;

static int acpi_checksum_ok(const void *ptr, size_t len) {
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) sum = (uint8_t)(sum + bytes[i]);
    return sum == 0;
}

static int acpi_memcmp(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if ((uint8_t)a[i] != (uint8_t)b[i]) return (int)((uint8_t)a[i] - (uint8_t)b[i]);
    }
    return 0;
}

static uint8_t *acpi_find_rsdp(void) {
    uint16_t ebda_seg = *(volatile uint16_t *)(uintptr_t)0x40E;
    uintptr_t ebda = ((uintptr_t)ebda_seg) << 4;

    if (ebda >= 0x80000 && ebda < 0xA0000) {
        for (uintptr_t addr = ebda; addr < ebda + 1024; addr += 16) {
            RsdpDescriptor *rsdp = (RsdpDescriptor *)addr;
            if (acpi_memcmp(rsdp->signature, "RSD PTR ", 8) == 0 &&
                acpi_checksum_ok(rsdp, 20)) {
                return (uint8_t *)rsdp;
            }
        }
    }

    for (uintptr_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        RsdpDescriptor *rsdp = (RsdpDescriptor *)addr;
        if (acpi_memcmp(rsdp->signature, "RSD PTR ", 8) == 0 &&
            acpi_checksum_ok(rsdp, 20)) {
            return (uint8_t *)rsdp;
        }
    }

    return (uint8_t *)0;
}

static AcpiSdtHeader *acpi_find_table(AcpiSdtHeader *rsdt, const char *sig) {
    if (!rsdt || acpi_memcmp(rsdt->signature, "RSDT", 4) != 0) return (AcpiSdtHeader *)0;
    uint32_t entries = (rsdt->length - sizeof(AcpiSdtHeader)) / 4;
    uint32_t *table_ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(AcpiSdtHeader));

    for (uint32_t i = 0; i < entries; i++) {
        AcpiSdtHeader *hdr = (AcpiSdtHeader *)(uintptr_t)table_ptrs[i];
        if (hdr && acpi_memcmp(hdr->signature, sig, 4) == 0) return hdr;
    }
    return (AcpiSdtHeader *)0;
}

static int acpi_parse_s5(uint8_t *dsdt, uint32_t length) {
    if (!dsdt || length < sizeof(AcpiSdtHeader)) return 0;

    for (uint32_t i = sizeof(AcpiSdtHeader); i + 7 < length; i++) {
        if (acpi_memcmp((const char *)(dsdt + i), "_S5_", 4) != 0) continue;

        if (i < 1) continue;
        if (!(dsdt[i - 1] == 0x08 || (i >= 2 && dsdt[i - 2] == '\\' && dsdt[i - 1] == 0x08))) {
            continue;
        }
        if (dsdt[i + 4] != 0x12) continue;

        uint8_t *pkg = dsdt + i + 5;
        pkg += ((pkg[0] & 0xC0) >> 6) + 2;

        if (*pkg == 0x0A) pkg++;
        g_slp_typa = (uint16_t)(*pkg << 10);
        pkg++;
        if (*pkg == 0x0A) pkg++;
        g_slp_typb = (uint16_t)(*pkg << 10);
        return 1;
    }

    return 0;
}

bool acpi_init(void) {
    uint8_t *rsdp_ptr = acpi_find_rsdp();
    if (!rsdp_ptr) {
        serial_print("[acpi] RSDP not found\n");
        g_acpi_ready = 0;
        return false;
    }

    RsdpDescriptor *rsdp = (RsdpDescriptor *)rsdp_ptr;
    AcpiSdtHeader *rsdt = (AcpiSdtHeader *)(uintptr_t)rsdp->rsdt_address;
    if (!rsdt || acpi_memcmp(rsdt->signature, "RSDT", 4) != 0 || !acpi_checksum_ok(rsdt, rsdt->length)) {
        serial_print("[acpi] invalid RSDT\n");
        g_acpi_ready = 0;
        return false;
    }

    AcpiSdtHeader *fadt = acpi_find_table(rsdt, "FACP");
    if (!fadt || !acpi_checksum_ok(fadt, fadt->length)) {
        serial_print("[acpi] FADT not found\n");
        g_acpi_ready = 0;
        return false;
    }

    uint8_t *fadt_bytes = (uint8_t *)fadt;
    uint32_t dsdt_addr = *(uint32_t *)(void *)(fadt_bytes + 40);
    g_pm1a_cnt = *(uint32_t *)(void *)(fadt_bytes + 64);
    g_pm1b_cnt = *(uint32_t *)(void *)(fadt_bytes + 68);
    g_reset_reg = *(AcpiGas *)(void *)(fadt_bytes + 116);
    g_reset_value = *(uint8_t *)(void *)(fadt_bytes + 128);

    uint64_t x_dsdt = 0;
    if (fadt->length >= 148) {
        x_dsdt = *(uint64_t *)(void *)(fadt_bytes + 140);
    }
    if (x_dsdt != 0) dsdt_addr = (uint32_t)x_dsdt;

    AcpiSdtHeader *dsdt = (AcpiSdtHeader *)(uintptr_t)dsdt_addr;
    if (!dsdt || acpi_memcmp(dsdt->signature, "DSDT", 4) != 0 || !acpi_checksum_ok(dsdt, dsdt->length)) {
        serial_print("[acpi] invalid DSDT\n");
        g_acpi_ready = 0;
        return false;
    }

    if (!acpi_parse_s5((uint8_t *)dsdt, dsdt->length)) {
        serial_print("[acpi] _S5 not found\n");
        g_acpi_ready = 0;
        return false;
    }

    g_acpi_ready = (g_pm1a_cnt != 0 && g_slp_typa != 0) ? 1 : 0;
    if (g_acpi_ready) {
        serial_print("[acpi] power management ready\n");
        return true;
    }

    serial_print("[acpi] PM control block missing\n");
    return false;
}

bool acpi_ready(void) {
    return g_acpi_ready != 0;
}

void acpi_shutdown(void) {
    const uint16_t slp_en = 1u << 13;

    if (g_acpi_ready) {
        outw_acpi(g_pm1a_cnt, (uint16_t)(g_slp_typa | slp_en));
        if (g_pm1b_cnt) outw_acpi(g_pm1b_cnt, (uint16_t)(g_slp_typb | slp_en));
    }

    outw_acpi(0x604, 0x2000);
    outw_acpi(0x4004, 0x2000);
    outw_acpi(0xB004, 0x2000);

    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}

void acpi_reboot(void) {
    if (g_reset_reg.address_space == 1 && g_reset_reg.address != 0) {
        outb_acpi((uint16_t)g_reset_reg.address, g_reset_value);
    }

    outb_acpi(0xCF9, 0x02);
    outb_acpi(0xCF9, 0x06);
}
