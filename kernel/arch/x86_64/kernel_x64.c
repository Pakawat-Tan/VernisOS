#include <stdint.h>
#include <stddef.h>
#include "ipc.h"
#include "module.h"
#include "sandbox.h"
#include "cli.h"
#include "ai_bridge.h"
#include "acpi.h"
#include "vfs.h"
#include "bcache.h"
#include "userdb.h"
#include "auditlog.h"
#include "klog.h"

#include "tcp.h"

// =============================================================================
// VGA Text Mode
// =============================================================================

// Boot info from bootloader (VBE framebuffer)
struct boot_info {
    uint32_t magic;
    uint32_t fb_addr;
    uint32_t fb_addr_high;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint32_t fb_bpp;
    uint32_t fb_type;   // 0 = VGA text, 1 = framebuffer
};
static int display_mode = 0;  // 0 = VGA text, 1 = framebuffer, 2 = GUI

// Rust framebuffer/console FFI
extern void fb_init(uint64_t addr, uint32_t width, uint32_t height,
                    uint32_t pitch, uint32_t bpp);
extern void console_init(uint32_t width, uint32_t height);
extern void console_putchar(uint8_t c);
extern void console_clear(void);
extern void console_set_color_vga(uint8_t fg, uint8_t bg);
extern void console_set_pos(uint32_t row, uint32_t col);
extern void console_get_pos(uint32_t *row, uint32_t *col);
extern void console_clear_to_eol(uint32_t row, uint32_t col);

// Rust mouse FFI
extern void mouse_init(uint32_t screen_w, uint32_t screen_h);
extern void mouse_handle_packet(uint8_t flags, int8_t dx, int8_t dy);
extern int32_t mouse_get_x(void);
extern int32_t mouse_get_y(void);
extern uint8_t mouse_get_buttons(void);

// Rust GUI FFI
extern void gui_init(uint32_t screen_w, uint32_t screen_h);
extern void gui_main_loop_tick(void);
extern void gui_handle_key(uint8_t scancode, uint8_t pressed);
extern void gui_handle_mouse(uint8_t flags, int8_t dx, int8_t dy);

#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GREY    7
#define VGA_COLOR_DARK_GREY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_LIGHT_BROWN   14
#define VGA_COLOR_WHITE         15
#define VGA_COLOR_YELLOW        VGA_COLOR_LIGHT_BROWN

static const size_t VGA_WIDTH  = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

static size_t   terminal_row;
static size_t   terminal_column;
static uint8_t  terminal_color;
static uint16_t *terminal_buffer;

static inline uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}
static inline uint16_t make_vgaentry(char c, uint8_t color) {
    return (uint16_t)(unsigned char)c | ((uint16_t)color << 8);
}

void serial_print(const char *s);  // forward declaration — defined later

static void terminal_initialize(void) {
    terminal_row    = 0;
    terminal_column = 0;
    terminal_color  = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;
}

static void terminal_setcolor(uint8_t color) { terminal_color = color; }

static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    terminal_buffer[y * VGA_WIDTH + x] = make_vgaentry(c, color);
}

static void terminal_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            terminal_buffer[y * VGA_WIDTH + x] = terminal_buffer[(y + 1) * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
    terminal_row = VGA_HEIGHT - 1;
}

// Rust GUI terminal FFI
extern void gui_terminal_putchar(uint8_t ch);
extern void gui_terminal_clear(void);

static void terminal_putchar(char c) {
    if (display_mode == 2) { gui_terminal_putchar((uint8_t)c); return; }
    if (display_mode == 1) { console_putchar((uint8_t)c); return; }
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row >= VGA_HEIGHT) terminal_scroll();
        return;
    }
    if (c == '\r') { terminal_column = 0; return; }
    if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
        }
        return;
    }

    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    if (++terminal_column >= VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row >= VGA_HEIGHT) terminal_scroll();
    }
}

static void terminal_writestring(const char *data) {
    for (size_t i = 0; data[i] != '\0'; i++)
        terminal_putchar(data[i]);
}

// =============================================================================
// VGA Public API — used by cli.c and other modules
// =============================================================================

void vga_print(const char *s) {
    terminal_writestring(s);
    serial_print(s);   // mirror CLI output to serial so -serial stdio shows it
}

static void vga_print_uint(uint32_t v, int base) {
    if (v == 0) { terminal_putchar('0'); return; }
    char buf[12];
    int i = 0;
    while (v) { buf[i++] = "0123456789abcdef"[v % (uint32_t)base]; v /= (uint32_t)base; }
    for (int j = i - 1; j >= 0; j--) terminal_putchar(buf[j]);
}

void vga_print_hex(uint32_t v) {
    terminal_writestring("0x");
    vga_print_uint(v, 16);
}

void vga_print_dec(uint32_t v) {
    vga_print_uint(v, 10);
}

void vga_set_pos(size_t row, size_t col) {
    if (display_mode >= 2) return;  // GUI manages its own cursor
    if (display_mode == 1) { console_set_pos((uint32_t)row, (uint32_t)col); return; }
    terminal_row    = row;
    terminal_column = col;
}

void vga_get_pos(size_t *row, size_t *col) {
    if (display_mode >= 2) { *row = 0; *col = 0; return; }
    if (display_mode == 1) {
        uint32_t r, c;
        console_get_pos(&r, &c);
        *row = (size_t)r; *col = (size_t)c;
        return;
    }
    *row = terminal_row;
    *col = terminal_column;
}

void vga_clear_to_eol(size_t row, size_t col) {
    if (display_mode >= 2) return;
    if (display_mode == 1) { console_clear_to_eol((uint32_t)row, (uint32_t)col); return; }
    for (size_t x = col; x < VGA_WIDTH; x++)
        terminal_buffer[row * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
}

void vga_clear_screen(void) {
    if (display_mode >= 2) { gui_terminal_clear(); return; }
    if (display_mode == 1) { console_clear(); return; }
    uint16_t blank = make_vgaentry(' ', make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        terminal_buffer[i] = blank;
    terminal_row    = 0;
    terminal_column = 0;
    terminal_color  = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

// =============================================================================
// TCP Stack Integration (Phase 49)
// =============================================================================

// Called at kernel init
__attribute__((constructor))
static void kernel_tcp_init(void) {
    tcp_init();
}

// Called from timer IRQ (should be called at 240Hz)
void kernel_tick_hook(void) {
    bcache_tick();
    tcp_tick();
}

static void clear_screen(void) {
    uint16_t blank = make_vgaentry(' ', make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        terminal_buffer[i] = blank;
}

// Print at specific row (for status lines)
static void terminal_print_at_row(size_t row, const char *str, uint8_t color) {
    size_t col = 0;
    for (size_t i = 0; str[i] && col < VGA_WIDTH; i++, col++)
        terminal_buffer[row * VGA_WIDTH + col] = make_vgaentry(str[i], color);
    while (col < VGA_WIDTH)
        terminal_buffer[row * VGA_WIDTH + col++] = make_vgaentry(' ', color);
}

// =============================================================================
// Port I/O helpers
// =============================================================================

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %w1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %w1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) {
    outb(0x80, 0);
}

void vga_set_cursor(size_t row, size_t col) {
    if (display_mode >= 1) return;
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

void vga_enable_cursor(void) {
    if (display_mode == 1) return;
    outb(0x3D4, 0x0A); outb(0x3D5, 0x00);  // cursor start line 0, enabled
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);  // cursor end line 15
}

// =============================================================================
// System Power Control
// =============================================================================

void system_shutdown(void) {
    acpi_shutdown();
}

void system_restart(void) {
    acpi_reboot();

    // Fallback: pulse CPU reset via keyboard controller (port 0x64, cmd 0xFE)
    uint8_t tmp;
    do { tmp = inb(0x64); } while (tmp & 0x02);
    outb(0x64, 0xFE);

    // Final fallback: triple fault → CPU reset
    __asm__ volatile("cli");
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = {0, 0};
    __asm__ volatile("lidt %0" :: "m"(null_idt));
    __asm__ volatile("int $0");  // no handler → triple fault → CPU reset
    while (1) __asm__ volatile("hlt");
}

// =============================================================================
// Phase 1: Serial Port (COM1 / UART 16550)
// =============================================================================

#define SERIAL_COM1 0x3F8

static void serial_init(void) {
    outb(SERIAL_COM1 + 1, 0x00); // Disable interrupts
    outb(SERIAL_COM1 + 3, 0x80); // Enable DLAB (set baud rate divisor)
    outb(SERIAL_COM1 + 0, 0x01); // Baud rate divisor lo = 1 → 115200
    outb(SERIAL_COM1 + 1, 0x00); // Baud rate divisor hi
    outb(SERIAL_COM1 + 3, 0x03); // 8 bits, no parity, 1 stop bit
    outb(SERIAL_COM1 + 2, 0xC7); // Enable FIFO, clear, 14-byte threshold
    outb(SERIAL_COM1 + 4, 0x0B); // IRQs on, RTS/DSR set
}

static void serial_putchar(char c) {
    while (!(inb(SERIAL_COM1 + 5) & 0x20)); // Wait TX buffer empty
    outb(SERIAL_COM1, c);
}

void serial_print(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putchar('\r');
        serial_putchar(*s++);
    }
}

void serial_print_uint(uint64_t n) {
    if (n == 0) { serial_putchar('0'); return; }
    char buf[21]; int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (--i >= 0) serial_putchar(buf[i]);
}

void serial_print_hex(uint64_t n) {
    const char *hex = "0123456789ABCDEF";
    serial_print("0x");
    int leading = 1;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (n >> i) & 0xF;
        if (nibble || !leading || i == 0) {
            serial_putchar(hex[nibble]);
            leading = 0;
        }
    }
}

// Wrapper for ipc.c which declares serial_print_dec(uint32_t)
void serial_print_dec(uint32_t val) { serial_print_uint((uint64_t)val); }

// =============================================================================
// Phase 2: GDT, TSS, IDT
// =============================================================================

// ---- GDT Entries ----

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) GdtEntry;

// 16-byte TSS descriptor (two consecutive GDT slots)
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;      // 0x89 = Present, type=0x9 (64-bit TSS available)
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) TssDescriptor;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) GdtPtr;

// 64-bit TSS structure (Intel Vol. 3A §7.7)
typedef struct {
    uint32_t reserved0;
    uint64_t rsp[3];        // Kernel stacks for privilege levels 0-2
    uint64_t reserved1;
    uint64_t ist[7];        // IST1-IST7
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed)) Tss64;

static Tss64 kernel_tss;
static GdtPtr gdt_ptr;

// Stacks for TSS IST and kernel stack
#define IST1_STACK_SIZE 0x2000  // 8KB double fault stack
#define IST2_STACK_SIZE 0x1000  // 4KB NMI stack
#define KERNEL_RSP0_SIZE 0x4000 // 16KB kernel ring-0 stack

static uint8_t ist1_stack[IST1_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist2_stack[IST2_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t kernel_rsp0_stack[KERNEL_RSP0_SIZE] __attribute__((aligned(16)));

static void gdt_set_entry(GdtEntry *e, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t granularity) {
    e->base_low    = base & 0xFFFF;
    e->base_mid    = (base >> 16) & 0xFF;
    e->base_high   = (base >> 24) & 0xFF;
    e->limit_low   = limit & 0xFFFF;
    e->granularity = (granularity & 0xF0) | ((limit >> 16) & 0x0F);
    e->access      = access;
}

static void tss_set_descriptor(TssDescriptor *d, uint64_t base, uint32_t limit) {
    d->limit_low   = limit & 0xFFFF;
    d->base_low    = base & 0xFFFF;
    d->base_mid    = (base >> 16) & 0xFF;
    d->access      = 0x89; // Present, DPL=0, type=TSS available (0x9)
    d->granularity = (limit >> 16) & 0x0F;
    d->base_high   = (base >> 24) & 0xFF;
    d->base_upper  = (base >> 32) & 0xFFFFFFFF;
    d->reserved    = 0;
}

// Combined GDT region (gdt_table + tss_desc adjacent in memory)
// We lay it out as: [5 GdtEntry][1 TssDescriptor]
static struct {
    GdtEntry entries[5];
    TssDescriptor tss;
} __attribute__((packed)) gdt_region;

static void gdt_init(void) {
    // Null descriptor
    gdt_set_entry(&gdt_region.entries[0], 0, 0, 0, 0);
    // Kernel code (64-bit): L=1, access=0x9A (present, code, readable, DPL=0)
    gdt_set_entry(&gdt_region.entries[1], 0, 0xFFFFF, 0x9A, 0xA0);
    // Kernel data: access=0x92 (present, data, writable, DPL=0)
    gdt_set_entry(&gdt_region.entries[2], 0, 0xFFFFF, 0x92, 0xC0);
    // User code (64-bit): DPL=3, access=0xFA
    gdt_set_entry(&gdt_region.entries[3], 0, 0xFFFFF, 0xFA, 0xA0);
    // User data: DPL=3, access=0xF2
    gdt_set_entry(&gdt_region.entries[4], 0, 0xFFFFF, 0xF2, 0xC0);

    // Setup TSS
    for (size_t i = 0; i < sizeof(Tss64); i++)
        ((uint8_t*)&kernel_tss)[i] = 0;
    kernel_tss.rsp[0]  = (uint64_t)(kernel_rsp0_stack + KERNEL_RSP0_SIZE);
    kernel_tss.ist[0]  = (uint64_t)(ist1_stack + IST1_STACK_SIZE);
    kernel_tss.ist[1]  = (uint64_t)(ist2_stack + IST2_STACK_SIZE);
    kernel_tss.iopb_offset = sizeof(Tss64);

    tss_set_descriptor(&gdt_region.tss, (uint64_t)&kernel_tss, sizeof(Tss64) - 1);

    gdt_ptr.limit = sizeof(gdt_region) - 1;
    gdt_ptr.base  = (uint64_t)&gdt_region;

    __asm__ volatile(
        "lgdt %0\n"
        "mov $0x10, %%ax\n"   // Kernel data selector (index 2)
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        // Far jump to reload CS with kernel code selector (index 1 = 0x08)
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        : : "m"(gdt_ptr) : "rax", "memory"
    );

    // Load TSS: selector is byte offset into GDT = 5 * 8 = 0x28
    __asm__ volatile("ltr %0" : : "r"((uint16_t)0x28));
}

// ---- IDT ----

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) IdtEntry;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) IdtPtr;

// Interrupt frame (what CPU pushes + what our stub pushes)
typedef struct {
    // Pushed by isr_common_stub (in reverse order, so highest address first)
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    // Pushed by individual stubs (or CPU for error codes)
    uint64_t int_no;
    uint64_t error_code;
    // Pushed automatically by CPU
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) InterruptFrame;

#define IDT_SIZE 256
static IdtEntry idt_table[IDT_SIZE];
static IdtPtr   idt_ptr;

// Forward declarations for ISR/IRQ stubs defined in interrupts.asm
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

extern void isr_syscall(void);  // int 0x80

static void idt_set_gate(int vec, uint64_t handler, uint16_t sel,
                          uint8_t type_attr, uint8_t ist) {
    idt_table[vec].offset_low  = handler & 0xFFFF;
    idt_table[vec].selector    = sel;
    idt_table[vec].ist         = ist;
    idt_table[vec].type_attr   = type_attr;
    idt_table[vec].offset_mid  = (handler >> 16) & 0xFFFF;
    idt_table[vec].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt_table[vec].reserved    = 0;
}

static void idt_init(void) {
    // Exceptions (vectors 0-31)
    idt_set_gate( 0, (uint64_t)isr0,  0x08, 0x8E, 0);  // #DE
    idt_set_gate( 1, (uint64_t)isr1,  0x08, 0x8E, 0);  // #DB
    idt_set_gate( 2, (uint64_t)isr2,  0x08, 0x8E, 2);  // NMI  - IST2
    idt_set_gate( 3, (uint64_t)isr3,  0x08, 0x8F, 0);  // #BP  - trap, DPL=0
    idt_set_gate( 4, (uint64_t)isr4,  0x08, 0x8E, 0);
    idt_set_gate( 5, (uint64_t)isr5,  0x08, 0x8E, 0);
    idt_set_gate( 6, (uint64_t)isr6,  0x08, 0x8E, 0);  // #UD
    idt_set_gate( 7, (uint64_t)isr7,  0x08, 0x8E, 0);
    idt_set_gate( 8, (uint64_t)isr8,  0x08, 0x8E, 1);  // #DF  - IST1
    idt_set_gate( 9, (uint64_t)isr9,  0x08, 0x8E, 0);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E, 0);  // #TS
    idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E, 0);  // #NP
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E, 0);  // #SS
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E, 0);  // #GP
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E, 0);  // #PF
    idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E, 0);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E, 0);  // #MF
    idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E, 0);  // #AC
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E, 3);  // #MC  - IST3
    idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E, 0);  // #XM
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E, 0);
    idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E, 0);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E, 0);
    idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E, 0);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E, 0);
    idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E, 0);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E, 0);
    idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E, 0);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E, 0);
    idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E, 0);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E, 0);
    idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E, 0);

    // Hardware IRQs: IRQ0-15 → vectors 0x20-0x2F
    idt_set_gate(0x20, (uint64_t)irq0,  0x08, 0x8E, 0);  // timer
    idt_set_gate(0x21, (uint64_t)irq1,  0x08, 0x8E, 0);  // keyboard
    idt_set_gate(0x22, (uint64_t)irq2,  0x08, 0x8E, 0);
    idt_set_gate(0x23, (uint64_t)irq3,  0x08, 0x8E, 0);
    idt_set_gate(0x24, (uint64_t)irq4,  0x08, 0x8E, 0);
    idt_set_gate(0x25, (uint64_t)irq5,  0x08, 0x8E, 0);
    idt_set_gate(0x26, (uint64_t)irq6,  0x08, 0x8E, 0);
    idt_set_gate(0x27, (uint64_t)irq7,  0x08, 0x8E, 0);
    idt_set_gate(0x28, (uint64_t)irq8,  0x08, 0x8E, 0);
    idt_set_gate(0x29, (uint64_t)irq9,  0x08, 0x8E, 0);
    idt_set_gate(0x2A, (uint64_t)irq10, 0x08, 0x8E, 0);
    idt_set_gate(0x2B, (uint64_t)irq11, 0x08, 0x8E, 0);
    idt_set_gate(0x2C, (uint64_t)irq12, 0x08, 0x8E, 0);
    idt_set_gate(0x2D, (uint64_t)irq13, 0x08, 0x8E, 0);
    idt_set_gate(0x2E, (uint64_t)irq14, 0x08, 0x8E, 0);
    idt_set_gate(0x2F, (uint64_t)irq15, 0x08, 0x8E, 0);

    // Syscall: int 0x80, DPL=3 trap gate
    idt_set_gate(0x80, (uint64_t)isr_syscall, 0x08, 0xEE, 0);

    idt_ptr.limit = sizeof(idt_table) - 1;
    idt_ptr.base  = (uint64_t)idt_table;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}

// =============================================================================
// Phase 2: PIC 8259A + PIT Timer
// =============================================================================

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_init(void) {
    // ICW1: start initialization, cascade, ICW4 needed
    outb(PIC1_CMD,  0x11); io_wait();
    outb(PIC2_CMD,  0x11); io_wait();
    // ICW2: remap IRQ0-7 → 0x20, IRQ8-15 → 0x28
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    // ICW3: master has slave on IRQ2, slave ID=2
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    // ICW4: 8086 mode
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    // OCW1: enable IRQ0 (timer) and IRQ1 (keyboard); mask everything else
    // If framebuffer mode, also unmask IRQ2 (cascade) and IRQ12 (mouse)
    if (display_mode >= 1) {
        outb(PIC1_DATA, 0xF8);  // 11111000 = unmask IRQ0, IRQ1, IRQ2(cascade)
        outb(PIC2_DATA, 0xEF);  // 11101111 = unmask IRQ12 (mouse)
    } else {
        outb(PIC1_DATA, 0xFC);  // 11111100 = mask all except IRQ0, IRQ1
        outb(PIC2_DATA, 0xFF);  // mask all slave IRQs
    }
}

static void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQUENCY 1193182UL
#define TIMER_HZ 240
#define SYS_USER_TEST 0xF0  // Phase 17: Ring 3 heartbeat sentinel syscall

// Phase 25: VernisFS-backed file I/O syscalls (path-based)
#define SYS_READ      64
#define SYS_WRITE     65

// Phase 20: Process lifecycle syscalls
#define SYS_EXIT      60
#define SYS_WAITPID   61
#define SYS_GETPID    62

// Phase 23: Signals
#define SYS_KILL      63

// Phase 41: File descriptor syscalls
#define SYS_OPEN      66
#define SYS_READ_FD   67
#define SYS_WRITE_FD  68
#define SYS_CLOSE     69
#define SYS_DUP       70
#define SYS_DUP2      71
#define SYS_PIPE      72

// Phase 43: Process creation
#define SYS_FORK      73
#define SYS_EXECVE    74
#define SYS_SBRK      75

// Phase 46: mmap + demand paging
#define SYS_MMAP      76
#define SYS_MUNMAP    77

#define SYS_SYNC      78    // Phase 48: Flush block cache

#define USER_VADDR_MIN_64 0x10000000ULL
#define USER_VADDR_MAX_64 0x40000000ULL
#define SYS_IO_PATH_MAX   64U
#define SYS_IO_BUF_MAX    4096U

static int user_ptr_range_valid_64(uint64_t addr, uint64_t len) {
    if (addr == 0 || len == 0) return 0;
    uint64_t end = addr + len;
    if (end < addr) return 0;
    return addr >= USER_VADDR_MIN_64 && end <= USER_VADDR_MAX_64;
}

static int copy_user_path_64(char *dst, uint64_t user_ptr) {
    if (!dst || user_ptr == 0) return -1;
    if (user_ptr < USER_VADDR_MIN_64 || user_ptr >= USER_VADDR_MAX_64) return -1;

    const char *src = (const char *)(uintptr_t)user_ptr;
    for (uint32_t i = 0; i < SYS_IO_PATH_MAX; i++) {
        char c = src[i];
        dst[i] = c;
        if (c == '\0') return 0;
    }
    dst[SYS_IO_PATH_MAX - 1] = '\0';
    return -1;
}

static int64_t syscall_vfs_read_64(uint64_t path_ptr, uint64_t user_buf_ptr, uint64_t max_len) {
    if (max_len == 0 || max_len > SYS_IO_BUF_MAX) return -1;
    if (!user_ptr_range_valid_64(user_buf_ptr, max_len)) return -1;

    char path[SYS_IO_PATH_MAX];
    if (copy_user_path_64(path, path_ptr) < 0) return -1;

    static uint8_t kbuf[SYS_IO_BUF_MAX];
    int n = kfs_read_file(path, kbuf, (size_t)max_len);
    if (n < 0) return -1;

    uint8_t *dst = (uint8_t *)(uintptr_t)user_buf_ptr;
    for (int i = 0; i < n; i++) dst[i] = kbuf[i];
    return n;
}

static int64_t syscall_vfs_write_64(uint64_t path_ptr, uint64_t user_buf_ptr, uint64_t len) {
    if (len == 0 || len > SYS_IO_BUF_MAX) return -1;
    if (!user_ptr_range_valid_64(user_buf_ptr, len)) return -1;

    char path[SYS_IO_PATH_MAX];
    if (copy_user_path_64(path, path_ptr) < 0) return -1;

    static uint8_t kbuf[SYS_IO_BUF_MAX];
    const uint8_t *src = (const uint8_t *)(uintptr_t)user_buf_ptr;
    for (uint64_t i = 0; i < len; i++) kbuf[i] = src[i];

    return kfs_write_file(path, kbuf, (size_t)len);
}

static void pit_init(void) {
    uint32_t divisor = PIT_FREQUENCY / TIMER_HZ;
    // Mode 2 (rate generator), lo/hi byte access, binary
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

// =============================================================================
// Timer tick counter (exported for Rust Instant::now)
// =============================================================================

volatile uint64_t kernel_tick = 0;

uint64_t get_kernel_tick(void) {
    return kernel_tick;
}

uint32_t kernel_get_timer_hz(void) {
    return TIMER_HZ;
}

// =============================================================================
// Phase 21: CMOS RTC (Real-Time Clock)
// =============================================================================

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

// Wait until CMOS update-in-progress flag (bit 7 of register 0x0A) clears
static void cmos_wait_ready(void) {
    while (cmos_read(0x0A) & 0x80) { /* spin */ }
}

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} RtcTime;

void rtc_read(RtcTime *t) {
    cmos_wait_ready();
    uint8_t sec  = cmos_read(0x00);
    uint8_t min  = cmos_read(0x02);
    uint8_t hr   = cmos_read(0x04);
    uint8_t day  = cmos_read(0x07);
    uint8_t mon  = cmos_read(0x08);
    uint8_t yr   = cmos_read(0x09);
    uint8_t regB = cmos_read(0x0B);

    // If BCD mode (bit 2 of register B clear), convert to binary
    if (!(regB & 0x04)) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hr  = bcd_to_bin(hr & 0x7F);  // mask AM/PM bit
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        yr  = bcd_to_bin(yr);
    }

    // If 12-hour mode (bit 1 of register B clear) and PM bit set
    if (!(regB & 0x02) && (cmos_read(0x04) & 0x80)) {
        hr = ((hr & 0x7F) + 12) % 24;
    }

    t->second = sec;
    t->minute = min;
    t->hour   = hr;
    t->day    = day;
    t->month  = mon;
    t->year   = 2000 + yr;  // CMOS year register is 0-99
}

// Exported for CLI
void kernel_rtc_read(uint8_t *hour, uint8_t *min, uint8_t *sec,
                     uint8_t *day, uint8_t *month, uint16_t *year) {
    RtcTime t;
    rtc_read(&t);
    *hour  = t.hour;
    *min   = t.minute;
    *sec   = t.second;
    *day   = t.day;
    *month = t.month;
    *year  = t.year;
}

// =============================================================================
// Keyboard ring buffer (Phase 3)
// =============================================================================

#define KBD_BUFFER_SIZE 256

typedef struct {
    char     buffer[KBD_BUFFER_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint8_t  shift_held;
    uint8_t  ctrl_held;
    uint8_t  alt_held;
    uint8_t  caps_lock;
    uint8_t  extended;   // 0xE0 prefix received
} KeyboardState;

static KeyboardState kbd_state;

static const char scancode_ascii[128] = {
    0, 27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};

static const char scancode_ascii_shift[128] = {
    0, 27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' '
};

static void keyboard_init(void) {
    for (size_t i = 0; i < KBD_BUFFER_SIZE; i++) kbd_state.buffer[i] = 0;
    kbd_state.read_pos  = 0;
    kbd_state.write_pos = 0;
    kbd_state.shift_held = 0;
    kbd_state.ctrl_held  = 0;
    kbd_state.alt_held   = 0;
    kbd_state.caps_lock  = 0;
}

// =============================================================================
// PS/2 Mouse (Phase GUI)
// =============================================================================

static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

static void ps2_mouse_wait_input(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(0x64) & 0x02) == 0) return;
    }
}

static void ps2_mouse_wait_output(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(0x64) & 0x01) return;
    }
}

static void ps2_mouse_write(uint8_t data) {
    ps2_mouse_wait_input();
    outb(0x64, 0xD4);    // Tell controller: next byte goes to mouse
    ps2_mouse_wait_input();
    outb(0x60, data);
}

static uint8_t ps2_mouse_read(void) {
    ps2_mouse_wait_output();
    return inb(0x60);
}

static void ps2_mouse_init(void) {
    // Enable auxiliary device (mouse)
    ps2_mouse_wait_input();
    outb(0x64, 0xA8);

    // Enable IRQ12 in controller config
    ps2_mouse_wait_input();
    outb(0x64, 0x20);   // Read controller config
    uint8_t status = ps2_mouse_read();
    status |= 0x02;     // Set bit 1: enable IRQ12
    ps2_mouse_wait_input();
    outb(0x64, 0x60);   // Write controller config
    ps2_mouse_wait_input();
    outb(0x60, status);

    // Set defaults
    ps2_mouse_write(0xF6);
    ps2_mouse_read();   // ACK

    // Enable data reporting
    ps2_mouse_write(0xF4);
    ps2_mouse_read();   // ACK

    mouse_cycle = 0;
    serial_print("[mouse] PS/2 mouse initialized\n");
}

static void mouse_irq_handler(void) {
    uint8_t data = inb(0x60);

    switch (mouse_cycle) {
    case 0:
        // First byte: flags. Bit 3 must be set (always-1 bit)
        if (data & 0x08) {
            mouse_packet[0] = data;
            mouse_cycle = 1;
        }
        break;
    case 1:
        mouse_packet[1] = data;
        mouse_cycle = 2;
        break;
    case 2:
        mouse_packet[2] = data;
        mouse_cycle = 0;
        {
            // Sign-extend dx and dy
            int8_t dx = (int8_t)mouse_packet[1];
            int8_t dy = (int8_t)mouse_packet[2];

            // Check overflow bits — discard if overflow
            if (!(mouse_packet[0] & 0xC0)) {
                if (display_mode == 2) {
                    gui_handle_mouse(mouse_packet[0], dx, dy);
                } else {
                    mouse_handle_packet(mouse_packet[0], dx, dy);
                }
            }
        }
        break;
    }
}

int keyboard_read_char(char *out) {
    if (kbd_state.read_pos == kbd_state.write_pos) return 0;
    *out = kbd_state.buffer[kbd_state.read_pos];
    kbd_state.read_pos = (kbd_state.read_pos + 1) % KBD_BUFFER_SIZE;
    return 1;
}

// Phase 42: forward declarations (defined after interrupt_dispatch section)
typedef struct KernelTTY_ KernelTTY;
static void tty_push_char(KernelTTY *tty, char c);
static KernelTTY kernel_tty0;

static void keyboard_handle_scancode(uint8_t scancode) {
    // Extended key prefix (0xE0) — next scancode is an extended key
    if (scancode == 0xE0) { kbd_state.extended = 1; return; }

    if (kbd_state.extended) {
        kbd_state.extended = 0;
        if (scancode & 0x80) return;  // extended key release — ignore
        char code = 0;
        switch (scancode) {
            case 0x48: code = (char)0x80; break;  // Up
            case 0x50: code = (char)0x81; break;  // Down
            case 0x4B: code = (char)0x82; break;  // Left
            case 0x4D: code = (char)0x83; break;  // Right
            case 0x53: code = (char)0x84; break;  // Delete
            case 0x47: code = (char)0x85; break;  // Home
            case 0x4F: code = (char)0x86; break;  // End
        }
        if (code) {
            uint32_t next = (kbd_state.write_pos + 1) % KBD_BUFFER_SIZE;
            if (next != kbd_state.read_pos) {
                kbd_state.buffer[kbd_state.write_pos] = code;
                kbd_state.write_pos = next;
            }
        }
        return;
    }

    if (scancode & 0x80) {
        // Key release
        uint8_t key = scancode & 0x7F;
        if (key == 0x2A || key == 0x36) kbd_state.shift_held = 0;
        if (key == 0x1D) kbd_state.ctrl_held = 0;
        if (key == 0x38) kbd_state.alt_held = 0;
        return;
    }

    // Key press
    if (scancode == 0x2A || scancode == 0x36) { kbd_state.shift_held = 1; return; }
    if (scancode == 0x1D) { kbd_state.ctrl_held = 1; return; }
    if (scancode == 0x38) { kbd_state.alt_held  = 1; return; }
    if (scancode == 0x3A) { kbd_state.caps_lock ^= 1; return; }

    if (scancode >= 128) return;

    uint8_t shift = kbd_state.shift_held ^ kbd_state.caps_lock;
    char c = shift ? scancode_ascii_shift[scancode] : scancode_ascii[scancode];
    if (!c) return;

    uint32_t next = (kbd_state.write_pos + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_state.read_pos) {
        kbd_state.buffer[kbd_state.write_pos] = c;
        kbd_state.write_pos = next;
    }
    // Phase 42: also push to TTY for user-space stdin
    tty_push_char(&kernel_tty0, c);
}

// =============================================================================
// Interrupt Dispatch (called by isr_common_stub in interrupts.asm)
// =============================================================================

static const char *exception_names[] = {
    "#DE Divide Error",        "#DB Debug",
    "NMI",                     "#BP Breakpoint",
    "#OF Overflow",            "#BR Bound Range",
    "#UD Invalid Opcode",      "#NM Device Not Available",
    "#DF Double Fault",        "Coprocessor Overrun",
    "#TS Invalid TSS",         "#NP Segment Not Present",
    "#SS Stack Fault",         "#GP General Protection",
    "#PF Page Fault",          "Reserved",
    "#MF x87 FPU Error",       "#AC Alignment Check",
    "#MC Machine Check",       "#XM SIMD Exception",
};

static inline uint64_t read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

static void log_page_fault_detail(uint64_t error_code) {
    uint64_t fault_addr = read_cr2();
    uint8_t present = (uint8_t)(error_code & 0x1);
    uint8_t write = (uint8_t)((error_code >> 1) & 0x1);
    uint8_t user = (uint8_t)((error_code >> 2) & 0x1);
    uint8_t rsvd = (uint8_t)((error_code >> 3) & 0x1);
    uint8_t ifetch = (uint8_t)((error_code >> 4) & 0x1);

    serial_print("[PF] cr2=");
    serial_print_hex(fault_addr);
    serial_print(" present=");
    serial_print_uint(present);
    serial_print(" write=");
    serial_print_uint(write);
    serial_print(" user=");
    serial_print_uint(user);
    serial_print(" rsvd=");
    serial_print_uint(rsvd);
    serial_print(" ifetch=");
    serial_print_uint(ifetch);
    serial_print("\n");

    terminal_writestring("[PF] addr=");
    vga_print_hex((uint32_t)fault_addr);
    terminal_writestring(" ");
    terminal_writestring(present ? "protection" : "non-present");
    terminal_writestring(" ");
    terminal_writestring(write ? "write" : "read");
    terminal_writestring(" ");
    terminal_writestring(user ? "user" : "kernel");
    if (rsvd) terminal_writestring(" rsvd");
    if (ifetch) terminal_writestring(" ifetch");
    terminal_writestring("\n");
}

// Forward declarations
uint64_t c_syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2,
                            uint64_t arg3, uint64_t arg4);
extern uint32_t scheduler_get_process_count(const void *sched);
extern uint32_t scheduler_get_current_pid(const void *sched);
extern int scheduler_kill_process(void *sched, uint32_t pid);
extern uint32_t scheduler_schedule(void *sched);
extern void *get_kernel_scheduler(void);
extern void scheduler_terminate_current(void *sched, int32_t exit_code);
extern int32_t scheduler_get_exit_code(const void *sched, size_t pid);

// Phase 18 types & state — must be visible to interrupt_dispatch below
#define MAX_TASKS          8
#define TASK_STACK_SIZE    0x4000   // 16 KB per task

// =============================================================================
// Phase 41: Per-process file descriptor table
// =============================================================================
#define FD_MAX            16
#define FD_TYPE_NONE       0
#define FD_TYPE_FILE       1   // VFS file (path-based open)
#define FD_TYPE_TTY        2   // Terminal device (stdin/stdout/stderr)
#define FD_TYPE_PIPE_R     3   // Read end of pipe
#define FD_TYPE_PIPE_W     4   // Write end of pipe

// Phase 42: Kernel TTY
#define TTY_BUF_SIZE     256

typedef struct KernelTTY_ {
    char     input_buf[TTY_BUF_SIZE];
    uint32_t input_read;
    uint32_t input_write;
    uint32_t input_count;
    uint8_t  cooked;             // 1 = line-buffered (cooked), 0 = raw
    char     line_buf[TTY_BUF_SIZE]; // cooked mode: accumulate until Enter
    uint32_t line_len;
    uint8_t  line_ready;         // 1 = line available for read
} KernelTTY;

static void tty_init(KernelTTY *tty) {
    for (int i = 0; i < TTY_BUF_SIZE; i++) {
        tty->input_buf[i] = 0; tty->line_buf[i] = 0;
    }
    tty->input_read = tty->input_write = tty->input_count = 0;
    tty->cooked = 1; tty->line_len = 0; tty->line_ready = 0;
}

static void tty_push_char(KernelTTY *tty, char c) {
    if (tty->cooked) {
        // Cooked mode: accumulate line, emit on \n
        if (c == '\n' || c == '\r') {
            // Copy line_buf to input ring
            for (uint32_t i = 0; i < tty->line_len && tty->input_count < TTY_BUF_SIZE; i++) {
                tty->input_buf[tty->input_write] = tty->line_buf[i];
                tty->input_write = (tty->input_write + 1) % TTY_BUF_SIZE;
                tty->input_count++;
            }
            // Add newline
            if (tty->input_count < TTY_BUF_SIZE) {
                tty->input_buf[tty->input_write] = '\n';
                tty->input_write = (tty->input_write + 1) % TTY_BUF_SIZE;
                tty->input_count++;
            }
            tty->line_len = 0;
            tty->line_ready = 1;
        } else if (c == '\b' || c == 127) {
            if (tty->line_len > 0) tty->line_len--;
        } else if (c == 3) {
            // Ctrl+C -> signal current process
            tty->line_len = 0;
        } else {
            if (tty->line_len < TTY_BUF_SIZE - 1)
                tty->line_buf[tty->line_len++] = c;
        }
    } else {
        // Raw mode: push directly
        if (tty->input_count < TTY_BUF_SIZE) {
            tty->input_buf[tty->input_write] = c;
            tty->input_write = (tty->input_write + 1) % TTY_BUF_SIZE;
            tty->input_count++;
        }
    }
}

static int tty_read(KernelTTY *tty, char *buf, int max) {
    int n = 0;
    while (n < max && tty->input_count > 0) {
        buf[n++] = tty->input_buf[tty->input_read];
        tty->input_read = (tty->input_read + 1) % TTY_BUF_SIZE;
        tty->input_count--;
    }
    if (tty->cooked && tty->input_count == 0) tty->line_ready = 0;
    return n;
}

static int tty_write(const char *buf, int len) {
    // Output to VGA + serial
    for (int i = 0; i < len; i++) {
        terminal_putchar(buf[i]);
        serial_putchar(buf[i]);
    }
    return len;
}

// Pipe buffer for SYS_PIPE
#define PIPE_BUF_SIZE  4096
#define MAX_PIPES       8

typedef struct {
    char buf[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint8_t  active;
    uint8_t  read_open;
    uint8_t  write_open;
} KernelPipe;

static KernelPipe kernel_pipes[MAX_PIPES];

typedef struct {
    uint8_t  type;                // FD_TYPE_*
    uint8_t  flags;               // O_RDONLY=1, O_WRONLY=2, O_RDWR=3
    char     path[64];            // VFS path (for FD_TYPE_FILE)
    uint32_t offset;              // file cursor offset
    uint8_t  pipe_idx;            // index into kernel_pipes (for PIPE types)
} FdEntry;

// =============================================================================
// Phase 46: VMA (Virtual Memory Area) for mmap / demand paging
// =============================================================================

#define VMA_MAX_PER_TASK 16
#define VMA_TYPE_NONE    0
#define VMA_TYPE_ANON    1
#define VMA_TYPE_FILE    2

#define PROT_READ    0x01
#define PROT_WRITE   0x02
#define PROT_EXEC    0x04
#define MAP_ANONYMOUS 0x10
#define MAP_PRIVATE   0x20

#define MMAP_BASE_64  0x20000000ULL

typedef struct {
    uint64_t start;
    uint64_t length;
    uint8_t  type;       // VMA_TYPE_*
    uint8_t  prot;       // PROT_READ | PROT_WRITE | PROT_EXEC
    uint8_t  flags;      // MAP_ANONYMOUS | MAP_PRIVATE
    uint8_t  _pad;
    char     path[64];   // file-backed: VFS path
    uint32_t file_offset;
} VmaEntry;

typedef struct {
    uint64_t rsp;                  // saved stack pointer (into stack[])
    uint32_t pid;                  // associated scheduler PID
    uint8_t  active;               // 1 = slot in use
    uint16_t ticks_remaining;      // ticks until preemption
    uint16_t ticks_total;          // full time-slice (reload value)
    FdEntry  fd_table[FD_MAX];     // Phase 41: per-process fd table
    uint32_t ppid_slot;            // Phase 43: parent task slot index (for fork)
    uint64_t brk;                  // Phase 43: program break for sbrk
    VmaEntry vma_list[VMA_MAX_PER_TASK]; // Phase 46: mmap regions
    uint64_t mmap_next;            // Phase 46: next mmap virtual address
    uint8_t  stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
} TaskSlot;

static TaskSlot  task_slots[MAX_TASKS];
static int       current_task_idx      = -1;
static int       context_switch_enabled = 0;
static uint64_t  context_switch_count  = 0;

// =============================================================================
// Phase 41: fd table helper functions
// =============================================================================

static void fd_table_init(FdEntry *fdt) {
    for (int i = 0; i < FD_MAX; i++) {
        fdt[i].type = FD_TYPE_NONE;
        fdt[i].flags = 0;
        fdt[i].path[0] = '\0';
        fdt[i].offset = 0;
        fdt[i].pipe_idx = 0;
    }
    // fd 0 = stdin (TTY read)
    fdt[0].type = FD_TYPE_TTY; fdt[0].flags = 1; // O_RDONLY
    // fd 1 = stdout (TTY write)
    fdt[1].type = FD_TYPE_TTY; fdt[1].flags = 2; // O_WRONLY
    // fd 2 = stderr (TTY write)
    fdt[2].type = FD_TYPE_TTY; fdt[2].flags = 2; // O_WRONLY
}

static int fd_alloc(FdEntry *fdt) {
    for (int i = 0; i < FD_MAX; i++)
        if (fdt[i].type == FD_TYPE_NONE) return i;
    return -1;
}

static int fd_alloc_at(FdEntry *fdt, int target) {
    if (target < 0 || target >= FD_MAX) return -1;
    if (fdt[target].type != FD_TYPE_NONE) return -1;
    return target;
}

static void fd_close_entry(FdEntry *fdt, int fd) {
    if (fd < 0 || fd >= FD_MAX) return;
    if (fdt[fd].type == FD_TYPE_PIPE_R || fdt[fd].type == FD_TYPE_PIPE_W) {
        uint8_t pidx = fdt[fd].pipe_idx;
        if (pidx < MAX_PIPES) {
            if (fdt[fd].type == FD_TYPE_PIPE_R) kernel_pipes[pidx].read_open = 0;
            else kernel_pipes[pidx].write_open = 0;
            if (!kernel_pipes[pidx].read_open && !kernel_pipes[pidx].write_open)
                kernel_pipes[pidx].active = 0;
        }
    }
    fdt[fd].type = FD_TYPE_NONE;
    fdt[fd].flags = 0;
    fdt[fd].path[0] = '\0';
    fdt[fd].offset = 0;
    fdt[fd].pipe_idx = 0;
}

static void fd_copy(FdEntry *dst, const FdEntry *src) {
    for (int i = 0; i < FD_MAX; i++) {
        dst[i] = src[i];
        // Increment pipe refcount (both ends stay open after fork)
    }
}

static int64_t sys_open(FdEntry *fdt, uint64_t path_ptr, uint64_t flags) {
    int fd = fd_alloc(fdt);
    if (fd < 0) return -1;
    char path[64];
    if (copy_user_path_64(path, path_ptr) < 0) return -1;
    // Check file exists in VFS
    const VfsFileEntry *fe = kfs_find_file(path);
    if (!fe) return -1;
    fdt[fd].type = FD_TYPE_FILE;
    fdt[fd].flags = (uint8_t)(flags & 0x3); // 1=RD,2=WR,3=RW
    if (fdt[fd].flags == 0) fdt[fd].flags = 1; // default read
    fdt[fd].offset = 0;
    // Copy path
    int pi = 0;
    while (path[pi] && pi < 63) { fdt[fd].path[pi] = path[pi]; pi++; }
    fdt[fd].path[pi] = '\0';
    return fd;
}

static int64_t sys_read_fd(FdEntry *fdt, int fd, uint64_t buf_ptr, uint64_t count) {
    if (fd < 0 || fd >= FD_MAX || fdt[fd].type == FD_TYPE_NONE) return -1;
    if (count == 0) return 0;
    if (count > SYS_IO_BUF_MAX) count = SYS_IO_BUF_MAX;
    if (!user_ptr_range_valid_64(buf_ptr, count)) return -1;

    if (fdt[fd].type == FD_TYPE_TTY) {
        // stdin read: block-wait for data from TTY
        // Non-blocking for now: return available data or 0
        char tmp[SYS_IO_BUF_MAX];
        int n = tty_read(&kernel_tty0, tmp, (int)count);
        if (n > 0) {
            char *ubuf = (char *)buf_ptr;
            for (int i = 0; i < n; i++) ubuf[i] = tmp[i];
        }
        return n;
    }
    if (fdt[fd].type == FD_TYPE_PIPE_R) {
        uint8_t pidx = fdt[fd].pipe_idx;
        if (pidx >= MAX_PIPES || !kernel_pipes[pidx].active) return -1;
        KernelPipe *p = &kernel_pipes[pidx];
        int n = 0;
        char *ubuf = (char *)buf_ptr;
        while (n < (int)count && p->count > 0) {
            ubuf[n++] = p->buf[p->read_pos];
            p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
            p->count--;
        }
        return n;
    }
    if (fdt[fd].type == FD_TYPE_FILE) {
        // Read from VFS file at current offset
        static uint8_t fd_tmp_buf[SYS_IO_BUF_MAX];
        int total = kfs_read_file(fdt[fd].path, fd_tmp_buf, SYS_IO_BUF_MAX);
        if (total < 0) return -1;
        int avail = total - (int)fdt[fd].offset;
        if (avail <= 0) return 0; // EOF
        int n = (int)count < avail ? (int)count : avail;
        uint8_t *ubuf = (uint8_t *)buf_ptr;
        for (int i = 0; i < n; i++)
            ubuf[i] = fd_tmp_buf[fdt[fd].offset + (uint32_t)i];
        fdt[fd].offset += (uint32_t)n;
        return n;
    }
    return -1;
}

static int64_t sys_write_fd(FdEntry *fdt, int fd, uint64_t buf_ptr, uint64_t count) {
    if (fd < 0 || fd >= FD_MAX || fdt[fd].type == FD_TYPE_NONE) return -1;
    if (count == 0) return 0;
    if (count > SYS_IO_BUF_MAX) count = SYS_IO_BUF_MAX;
    if (!user_ptr_range_valid_64(buf_ptr, count)) return -1;

    if (fdt[fd].type == FD_TYPE_TTY) {
        // stdout/stderr: output to terminal
        return (int64_t)tty_write((const char *)buf_ptr, (int)count);
    }
    if (fdt[fd].type == FD_TYPE_PIPE_W) {
        uint8_t pidx = fdt[fd].pipe_idx;
        if (pidx >= MAX_PIPES || !kernel_pipes[pidx].active) return -1;
        KernelPipe *p = &kernel_pipes[pidx];
        int n = 0;
        const char *ubuf = (const char *)buf_ptr;
        while (n < (int)count && p->count < PIPE_BUF_SIZE) {
            p->buf[p->write_pos] = ubuf[n++];
            p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
            p->count++;
        }
        return n;
    }
    if (fdt[fd].type == FD_TYPE_FILE) {
        // Write to VFS file
        static uint8_t fd_wr_buf[SYS_IO_BUF_MAX];
        const uint8_t *ubuf = (const uint8_t *)buf_ptr;
        for (int i = 0; i < (int)count; i++) fd_wr_buf[i] = ubuf[i];
        int rc = kfs_write_file(fdt[fd].path, fd_wr_buf, count);
        return rc < 0 ? -1 : (int64_t)count;
    }
    return -1;
}

static int64_t sys_close(FdEntry *fdt, int fd) {
    if (fd < 0 || fd >= FD_MAX || fdt[fd].type == FD_TYPE_NONE) return -1;
    fd_close_entry(fdt, fd);
    return 0;
}

static int64_t sys_dup(FdEntry *fdt, int oldfd) {
    if (oldfd < 0 || oldfd >= FD_MAX || fdt[oldfd].type == FD_TYPE_NONE) return -1;
    int newfd = fd_alloc(fdt);
    if (newfd < 0) return -1;
    fdt[newfd] = fdt[oldfd];
    return newfd;
}

static int64_t sys_dup2(FdEntry *fdt, int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= FD_MAX || fdt[oldfd].type == FD_TYPE_NONE) return -1;
    if (newfd < 0 || newfd >= FD_MAX) return -1;
    if (oldfd == newfd) return newfd;
    if (fdt[newfd].type != FD_TYPE_NONE) fd_close_entry(fdt, newfd);
    fdt[newfd] = fdt[oldfd];
    return newfd;
}

static int64_t sys_pipe(FdEntry *fdt, uint64_t fds_ptr) {
    if (!user_ptr_range_valid_64(fds_ptr, 8)) return -1;
    // Find a free pipe
    int pidx = -1;
    for (int i = 0; i < MAX_PIPES; i++)
        if (!kernel_pipes[i].active) { pidx = i; break; }
    if (pidx < 0) return -1;
    int rfd = fd_alloc(fdt);
    if (rfd < 0) return -1;
    fdt[rfd].type = FD_TYPE_PIPE_R;
    fdt[rfd].pipe_idx = (uint8_t)pidx;
    int wfd = fd_alloc(fdt);
    if (wfd < 0) { fdt[rfd].type = FD_TYPE_NONE; return -1; }
    fdt[wfd].type = FD_TYPE_PIPE_W;
    fdt[wfd].pipe_idx = (uint8_t)pidx;
    kernel_pipes[pidx].active = 1;
    kernel_pipes[pidx].read_open = 1;
    kernel_pipes[pidx].write_open = 1;
    kernel_pipes[pidx].read_pos = 0;
    kernel_pipes[pidx].write_pos = 0;
    kernel_pipes[pidx].count = 0;
    int32_t *ufds = (int32_t *)fds_ptr;
    ufds[0] = rfd; ufds[1] = wfd;
    return 0;
}

// =============================================================================
// Phase 43: fork() + exec() + sbrk()
// =============================================================================

// Forward declarations for symbols defined later in this file
static int elf_exec(const char *path, uint16_t ticks);
#ifndef PAGE_PRESENT
#define PAGE_PRESENT    0x01ULL
#define PAGE_WRITABLE   0x02ULL
#define PAGE_USER       0x04ULL
#define PAGE_PS         0x80ULL
#endif
static uint64_t frame_alloc(void);
static void paging_map_4k(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
static void paging_flush_tlb(void);
static uint64_t *kernel_pml4;
static uint8_t elf_load_buf[];
extern uint32_t scheduler_create_process(void *sched, uint8_t priority, const char *command);
// Phase 46 forward declarations
static VmaEntry *vma_find(VmaEntry *vmas, uint64_t addr);
static void vma_init(VmaEntry *vmas);
static int64_t sys_mmap(uint64_t length, uint64_t prot_flags, uint64_t path_ptr);
static int64_t sys_munmap(uint64_t addr, uint64_t length);
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

// sys_fork: create a child process that's a copy of the parent.
// Simplified fork — copies user pages, fd table, stack state.
// Returns child PID in parent, 0 in child, -1 on error.
static int64_t sys_fork(InterruptFrame *frame) {
    if (current_task_idx < 0) return -1;
    TaskSlot *parent = &task_slots[current_task_idx];

    // Find free slot
    int child_slot = -1;
    for (int i = 0; i < MAX_TASKS; i++)
        if (!task_slots[i].active) { child_slot = i; break; }
    if (child_slot < 0) return -1;

    // Create scheduler process
    void *sched = get_kernel_scheduler();
    if (!sched) return -1;
    uint32_t child_pid = scheduler_create_process(sched, 5, "forked");

    TaskSlot *child = &task_slots[child_slot];
    child->active = 1;
    child->pid = child_pid;
    child->ticks_remaining = parent->ticks_total;
    child->ticks_total = parent->ticks_total;
    child->ppid_slot = (uint32_t)current_task_idx;
    child->brk = parent->brk;

    // Copy fd table
    fd_copy(child->fd_table, parent->fd_table);

    // Copy VMA list (Phase 46)
    for (int i = 0; i < VMA_MAX_PER_TASK; i++)
        child->vma_list[i] = parent->vma_list[i];
    child->mmap_next = parent->mmap_next;

    // Copy kernel stack (contains the iretq frame for the child to resume)
    for (int i = 0; i < TASK_STACK_SIZE; i++)
        child->stack[i] = parent->stack[i];

    // The child's stack has the same InterruptFrame layout.
    // We compute the child's frame pointer relative to the child's stack.
    uint64_t frame_offset = (uint64_t)frame - (uint64_t)parent->stack;
    InterruptFrame *child_frame = (InterruptFrame *)(child->stack + frame_offset);

    // Child returns 0 from fork
    child_frame->rax = 0;

    // Set child's saved RSP to point into child's stack
    child->rsp = (uint64_t)child_frame;

    // Update TSS for child
    // (will be set when child is actually scheduled)

    // Parent returns child PID
    return (int64_t)child_pid;
}

// sys_execve: replace current process image with new ELF.
// execve(path_ptr, argv, envp) — argv/envp ignored for now.
static int64_t sys_execve(InterruptFrame *frame, uint64_t path_ptr) {
    if (current_task_idx < 0) return -1;
    char path[64];
    if (copy_user_path_64(path, path_ptr) < 0) return -1;

    // Read ELF from VFS
    int file_size = kfs_read_file(path, elf_load_buf, 128 * 1024);
    if (file_size < 64) return -1;

    // Validate ELF64
    typedef struct {
        uint32_t e_magic; uint8_t e_class; uint8_t e_data;
        uint8_t e_version; uint8_t e_osabi;
        uint8_t e_pad[8]; uint16_t e_type; uint16_t e_machine;
        uint32_t e_version2; uint64_t e_entry;
        uint64_t e_phoff; uint64_t e_shoff;
        uint32_t e_flags; uint16_t e_ehsize;
        uint16_t e_phentsize; uint16_t e_phnum;
        uint16_t e_shentsize; uint16_t e_shnum;
        uint16_t e_shstrndx;
    } __attribute__((packed)) ExecElf64_Ehdr;
    typedef struct {
        uint32_t p_type; uint32_t p_flags;
        uint64_t p_offset; uint64_t p_vaddr; uint64_t p_paddr;
        uint64_t p_filesz; uint64_t p_memsz; uint64_t p_align;
    } __attribute__((packed)) ExecElf64_Phdr;

    ExecElf64_Ehdr *ehdr = (ExecElf64_Ehdr *)elf_load_buf;
    if (ehdr->e_magic != 0x464C457F || ehdr->e_class != 2 ||
        ehdr->e_machine != 0x3E || ehdr->e_type != 2)
        return -1;

    // Map PT_LOAD segments (overwriting old user mappings)
    ExecElf64_Phdr *phdr = (ExecElf64_Phdr *)(elf_load_buf + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != 1) continue;
        if (phdr[i].p_memsz == 0) continue;
        uint64_t seg_vaddr = phdr[i].p_vaddr;
        uint64_t seg_filesz = phdr[i].p_filesz;
        uint64_t seg_memsz = phdr[i].p_memsz;
        uint64_t seg_foff = phdr[i].p_offset;
        uint64_t vaddr = seg_vaddr & ~0xFFFULL;
        uint64_t vend = (seg_vaddr + seg_memsz + 0xFFF) & ~0xFFFULL;
        for (uint64_t va = vaddr; va < vend; va += 0x1000) {
            uint64_t fr = frame_alloc();
            if (!fr) return -1;
            for (int b = 0; b < 0x1000; b++) {
                uint64_t abs_va = va + (uint64_t)b;
                if (abs_va < seg_vaddr || abs_va >= seg_vaddr + seg_memsz) continue;
                uint64_t seg_off = abs_va - seg_vaddr;
                if (seg_off < seg_filesz) {
                    uint64_t foff = seg_foff + seg_off;
                    if (foff < (uint64_t)file_size)
                        ((uint8_t *)fr)[b] = elf_load_buf[foff];
                }
            }
            paging_map_4k(kernel_pml4, va, fr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        // Track program break (end of last segment)
        uint64_t seg_end = seg_vaddr + seg_memsz;
        seg_end = (seg_end + 0xFFF) & ~0xFFFULL;
        if (seg_end > task_slots[current_task_idx].brk)
            task_slots[current_task_idx].brk = seg_end;
    }

    // Remap user stack
    for (uint64_t i = 0; i < 0x4000; i += 0x1000) {
        uint64_t fr = frame_alloc();
        if (!fr) return -1;
        paging_map_4k(kernel_pml4, 0x10800000ULL - 0x4000ULL + i, fr,
                      PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    paging_flush_tlb();

    // Reset fd table: keep fd 0/1/2, close rest
    FdEntry *fdt = task_slots[current_task_idx].fd_table;
    for (int i = 3; i < FD_MAX; i++) fd_close_entry(fdt, i);

    // Build new iretq frame on kernel stack — jump to new entry point
    // We modify the current interrupt frame to "return" to the new ELF entry
    frame->rip = ehdr->e_entry;
    frame->rsp = 0x10800000ULL;  // USER_STACK_TOP
    frame->rax = 0;
    frame->rbx = 0; frame->rcx = 0; frame->rdx = 0;
    frame->rsi = 0; frame->rdi = 0; frame->rbp = 0;

    serial_print("[execve] replaced with ");
    serial_print(path);
    serial_print(" entry=0x");
    serial_print_hex(ehdr->e_entry);
    serial_print("\n");
    return 0;
}

// sys_sbrk: grow/query program break (for malloc)
static int64_t sys_sbrk(int64_t increment) {
    if (current_task_idx < 0) return -1;
    TaskSlot *t = &task_slots[current_task_idx];
    if (t->brk == 0) t->brk = 0x10100000ULL; // default heap start
    uint64_t old_brk = t->brk;
    if (increment == 0) return (int64_t)old_brk;
    uint64_t new_brk = old_brk + (uint64_t)increment;
    // Map new pages if growing
    if (increment > 0) {
        uint64_t page_start = (old_brk + 0xFFF) & ~0xFFFULL;
        uint64_t page_end = (new_brk + 0xFFF) & ~0xFFFULL;
        for (uint64_t va = page_start; va < page_end; va += 0x1000) {
            uint64_t fr = frame_alloc();
            if (!fr) return -1;
            paging_map_4k(kernel_pml4, va, fr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        paging_flush_tlb();
    }
    t->brk = new_brk;
    return (int64_t)old_brk;
}

// =============================================================================
// Phase 46: VMA helpers + mmap / munmap syscalls
// =============================================================================

static VmaEntry *vma_find(VmaEntry *vmas, uint64_t addr) {
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (vmas[i].type != VMA_TYPE_NONE) {
            if (addr >= vmas[i].start && addr < vmas[i].start + vmas[i].length)
                return &vmas[i];
        }
    }
    return (VmaEntry *)0;
}

static void vma_init(VmaEntry *vmas) {
    for (int i = 0; i < VMA_MAX_PER_TASK; i++)
        vmas[i].type = VMA_TYPE_NONE;
}

// Temp buffer for file-backed demand paging (shared, only used in #PF handler)
static uint8_t mmap_file_tmp[65536];

static int64_t sys_mmap(uint64_t length, uint64_t prot_flags, uint64_t path_ptr) {
    if (current_task_idx < 0) return -1;
    TaskSlot *t = &task_slots[current_task_idx];

    length = (length + 0xFFF) & ~0xFFFULL;  // round up to page
    if (length == 0 || length > 0x1000000ULL) return -1;  // max 16MB per mapping

    uint8_t prot  = (uint8_t)(prot_flags & 0xFF);
    uint8_t flags = (uint8_t)((prot_flags >> 8) & 0xFF);

    // Find free VMA slot
    int slot = -1;
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (t->vma_list[i].type == VMA_TYPE_NONE) { slot = i; break; }
    }
    if (slot < 0) return -1;

    // Allocate virtual address range
    if (t->mmap_next == 0) t->mmap_next = MMAP_BASE_64;
    uint64_t va = t->mmap_next;
    t->mmap_next += length;

    VmaEntry *v = &t->vma_list[slot];
    v->start  = va;
    v->length = length;
    v->prot   = prot;
    v->flags  = flags;
    v->file_offset = 0;
    v->path[0] = '\0';

    if (path_ptr == 0 || (flags & MAP_ANONYMOUS)) {
        v->type = VMA_TYPE_ANON;
    } else {
        v->type = VMA_TYPE_FILE;
        if (copy_user_path_64(v->path, path_ptr) < 0) {
            v->type = VMA_TYPE_NONE;
            return -1;
        }
    }

    serial_print("[mmap] va=0x");
    serial_print_hex(va);
    serial_print(" len=0x");
    serial_print_hex(length);
    serial_print(v->type == VMA_TYPE_FILE ? " file=" : " anon\n");
    if (v->type == VMA_TYPE_FILE) {
        serial_print(v->path);
        serial_print("\n");
    }

    return (int64_t)va;
}

static int64_t sys_munmap(uint64_t addr, uint64_t length) {
    if (current_task_idx < 0) return -1;
    TaskSlot *t = &task_slots[current_task_idx];
    (void)length;

    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (t->vma_list[i].type != VMA_TYPE_NONE && t->vma_list[i].start == addr) {
            t->vma_list[i].type = VMA_TYPE_NONE;
            serial_print("[munmap] va=0x");
            serial_print_hex(addr);
            serial_print("\n");
            return 0;
        }
    }
    return -1;
}

uint64_t interrupt_dispatch(InterruptFrame *frame) {
    uint64_t vec = frame->int_no;

    if (vec == 0x20) {
        // IRQ0: Timer
        kernel_tick++;
        pic_send_eoi(0);

        // COM2 polling is simple port I/O — safe in IRQ
        const uint64_t cmd_poll_div = (TIMER_HZ / 10) ? (TIMER_HZ / 10) : 1;
        if ((kernel_tick % cmd_poll_div) == 0) {
            ai_poll_cmd();
        }

        // Phase 48: periodic dirty-block writeback
        bcache_tick();

        // Phase 18: preemptive context switch (pure C, no Rust calls)
        if (context_switch_enabled && current_task_idx >= 0) {
            TaskSlot *cur = &task_slots[current_task_idx];
            if (cur->ticks_remaining > 0)
                cur->ticks_remaining--;
            if (cur->ticks_remaining == 0) {
                cur->rsp             = (uint64_t)frame;
                cur->ticks_remaining = cur->ticks_total;

                // Round-robin: find next active task
                int next = current_task_idx;
                for (int tries = 0; tries < MAX_TASKS; tries++) {
                    next = (next + 1) % MAX_TASKS;
                    if (task_slots[next].active) break;
                }
                if (next != current_task_idx && task_slots[next].active) {
                    context_switch_count++;
                    current_task_idx = next;
                    // Phase 17: update TSS rsp[0] so that Ring 3 → Ring 0
                    // transitions land on the correct per-task kernel stack.
                    kernel_tss.rsp[0] = (uint64_t)(task_slots[next].stack + TASK_STACK_SIZE);
                    return task_slots[next].rsp;   // asm switches RSP
                }
            }
        }
        return 0;
    }

    if (vec == 0x21) {
        // IRQ1: PS/2 Keyboard
        uint8_t scancode = inb(0x60);
        if (display_mode == 2) {
            gui_handle_key(scancode, (scancode & 0x80) ? 0 : 1);
        }
        // Always feed CLI keyboard buffer so blocking CLI commands (e.g. ps)
        // can still receive input while GUI mode is active.
        keyboard_handle_scancode(scancode);
        pic_send_eoi(1);
        return 0;
    }

    if (vec == 0x2C) {
        // IRQ12: PS/2 Mouse
        mouse_irq_handler();
        pic_send_eoi(12);
        return 0;
    }

    if (vec >= 0x22 && vec <= 0x2F) {
        // Other hardware IRQs — just EOI
        pic_send_eoi((uint8_t)(vec - 0x20));
        return 0;
    }

    if (vec == 0x80) {
        // int 0x80 syscall: RAX=num, RBX=a1, RCX=a2, RDX=a3
        if (frame->rax == SYS_USER_TEST) {
            // Phase 17: heartbeat from Ring 3 user program
            static uint64_t user_test_count = 0;
            user_test_count++;
            if ((user_test_count % TIMER_HZ) == 1) {
                serial_print("[ring3] user heartbeat #");
                serial_print_uint(user_test_count);
                serial_print("\n");
            }
            frame->rax = 0;  // success
        } else if (frame->rax == SYS_EXIT) {
            // Phase 20: exit(exit_code) — terminate current process
            uint32_t exit_pid = 0;
            if (current_task_idx >= 0)
                exit_pid = task_slots[current_task_idx].pid;
            void *sched = get_kernel_scheduler();
            if (sched)
                scheduler_terminate_current(sched, (int32_t)frame->rbx);
            // Deactivate task slot and context-switch to next
            if (context_switch_enabled && current_task_idx >= 0) {
                task_slots[current_task_idx].active = 0;
                int next = -1;
                for (int i = 0; i < MAX_TASKS; i++) {
                    int idx = (current_task_idx + 1 + i) % MAX_TASKS;
                    if (task_slots[idx].active) { next = idx; break; }
                }
                serial_print("[exit] pid=");
                serial_print_uint(exit_pid);
                serial_print(" code=");
                serial_print_uint((uint32_t)frame->rbx);
                serial_print("\n");
                if (next >= 0) {
                    current_task_idx = next;
                    kernel_tss.rsp[0] = (uint64_t)(task_slots[next].stack + TASK_STACK_SIZE);
                    return task_slots[next].rsp;
                }
            }
            // No tasks left — halt
            serial_print("[exit] no more tasks — halt\n");
            __asm__ volatile("cli; hlt");
            return 0;
        } else if (frame->rax == SYS_WAITPID) {
            // Phase 20: waitpid(pid) — non-blocking
            // Returns exit code (>=0) if terminated, -1 if still running/not found
            void *sched = get_kernel_scheduler();
            if (sched) {
                int32_t code = scheduler_get_exit_code(sched, (size_t)frame->rbx);
                frame->rax = (uint64_t)(int64_t)code;
            } else {
                frame->rax = (uint64_t)(int64_t)-1;
            }
        } else if (frame->rax == SYS_GETPID) {
            // Phase 20: getpid() — return current PID
            void *sched = get_kernel_scheduler();
            frame->rax = sched ? scheduler_get_current_pid(sched) : 0;
        } else if (frame->rax == SYS_KILL) {
            // Phase 23: kill(pid, sig) — send signal to process
            uint64_t dst_pid = frame->rbx;
            uint8_t sig = (uint8_t)frame->rcx;
            void *sched = get_kernel_scheduler();
            if (sched) {
                extern int scheduler_signal_send(void *sched, size_t dst_pid, uint8_t sig);
                frame->rax = (uint64_t)(int64_t)scheduler_signal_send(sched, dst_pid, sig);
            } else {
                frame->rax = (uint64_t)-1;
            }
        } else if (frame->rax == SYS_READ) {
            // Phase 25: read(path_ptr, user_buf_ptr, max_len) -> bytes read
            frame->rax = (uint64_t)syscall_vfs_read_64(frame->rbx, frame->rcx, frame->rdx);
        } else if (frame->rax == SYS_WRITE) {
            // Phase 25: write(path_ptr, user_buf_ptr, len) -> bytes written
            frame->rax = (uint64_t)syscall_vfs_write_64(frame->rbx, frame->rcx, frame->rdx);
        } else if (frame->rax == SYS_OPEN) {
            // Phase 41: open(path_ptr, flags) -> fd
            FdEntry *fdt = (current_task_idx >= 0) ? task_slots[current_task_idx].fd_table : (FdEntry*)0;
            frame->rax = fdt ? (uint64_t)sys_open(fdt, frame->rbx, frame->rcx) : (uint64_t)-1;
        } else if (frame->rax == SYS_READ_FD) {
            // Phase 41: read_fd(fd, buf_ptr, count) -> bytes
            FdEntry *fdt = (current_task_idx >= 0) ? task_slots[current_task_idx].fd_table : (FdEntry*)0;
            frame->rax = fdt ? (uint64_t)sys_read_fd(fdt, (int)frame->rbx, frame->rcx, frame->rdx) : (uint64_t)-1;
        } else if (frame->rax == SYS_WRITE_FD) {
            // Phase 41: write_fd(fd, buf_ptr, count) -> bytes
            FdEntry *fdt = (current_task_idx >= 0) ? task_slots[current_task_idx].fd_table : (FdEntry*)0;
            frame->rax = fdt ? (uint64_t)sys_write_fd(fdt, (int)frame->rbx, frame->rcx, frame->rdx) : (uint64_t)-1;
        } else if (frame->rax == SYS_CLOSE) {
            // Phase 41: close(fd) -> 0 or -1
            FdEntry *fdt = (current_task_idx >= 0) ? task_slots[current_task_idx].fd_table : (FdEntry*)0;
            frame->rax = fdt ? (uint64_t)sys_close(fdt, (int)frame->rbx) : (uint64_t)-1;
        } else if (frame->rax == SYS_DUP) {
            FdEntry *fdt = (current_task_idx >= 0) ? task_slots[current_task_idx].fd_table : (FdEntry*)0;
            frame->rax = fdt ? (uint64_t)sys_dup(fdt, (int)frame->rbx) : (uint64_t)-1;
        } else if (frame->rax == SYS_DUP2) {
            FdEntry *fdt = (current_task_idx >= 0) ? task_slots[current_task_idx].fd_table : (FdEntry*)0;
            frame->rax = fdt ? (uint64_t)sys_dup2(fdt, (int)frame->rbx, (int)frame->rcx) : (uint64_t)-1;
        } else if (frame->rax == SYS_PIPE) {
            FdEntry *fdt = (current_task_idx >= 0) ? task_slots[current_task_idx].fd_table : (FdEntry*)0;
            frame->rax = fdt ? (uint64_t)sys_pipe(fdt, frame->rbx) : (uint64_t)-1;
        } else if (frame->rax == SYS_FORK) {
            // Phase 43: fork() -> child_pid in parent, 0 in child
            frame->rax = (uint64_t)sys_fork(frame);
        } else if (frame->rax == SYS_EXECVE) {
            // Phase 43: execve(path_ptr) -> 0 on success (doesn't return), -1 on error
            int64_t rc = sys_execve(frame, frame->rbx);
            if (rc < 0) frame->rax = (uint64_t)-1;
            else return 0; // execve succeeded — return directly
        } else if (frame->rax == SYS_SBRK) {
            // Phase 43: sbrk(increment) -> old break
            frame->rax = (uint64_t)sys_sbrk((int64_t)frame->rbx);
        } else if (frame->rax == SYS_MMAP) {
            // Phase 46: mmap(length, prot_flags, path_ptr) -> mapped va
            frame->rax = (uint64_t)sys_mmap(frame->rbx, frame->rcx, frame->rdx);
        } else if (frame->rax == SYS_MUNMAP) {
            // Phase 46: munmap(addr, length) -> 0 or -1
            frame->rax = (uint64_t)sys_munmap(frame->rbx, frame->rcx);
        } else if (frame->rax == SYS_SYNC) {
            // Phase 48: sync() -> 0 or -1
            frame->rax = (uint64_t)bcache_sync();
        } else {
            frame->rax = c_syscall_handler(frame->rax, frame->rbx,
                                            frame->rcx, frame->rdx, 0);
        }
        return 0;
    }

    if (vec <= 19) {
        // CPU Exception
        const char *name = (vec < 20) ? exception_names[vec] : "Unknown";
        terminal_setcolor(make_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
        terminal_writestring("\n*** KERNEL EXCEPTION: ");
        terminal_writestring(name);
        terminal_writestring(" ***\n");

        serial_print("\n[EXCEPTION] ");
        serial_print(name);
        serial_print(" | vec=");
        serial_print_uint(vec);
        serial_print(" err=");
        serial_print_hex(frame->error_code);
        serial_print(" rip=");
        serial_print_hex(frame->rip);
        serial_print(" rsp=");
        serial_print_hex(frame->rsp);
        serial_print("\n");

        if (vec == 14) {
            log_page_fault_detail(frame->error_code);
        } else if (vec == 13) {
            serial_print("[GP] general protection fault detected\n");
        } else if (vec == 8) {
            serial_print("[DF] double fault detected\n");
        }

        // Phase 10: Notify AI of CPU exception
        {
            char evbuf[64];
            char vecbuf[12]; char ripbuf[20];
            // vec as decimal
            uint64_t v = vec; int i = 0; char tmp[12];
            if (v == 0) { tmp[i++] = '0'; }
            else { while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; } }
            int p = 0;
            for (int j = i - 1; j >= 0; j--) vecbuf[p++] = tmp[j];
            vecbuf[p] = '\0';
            // rip as hex
            uint64_t r = frame->rip;
            ripbuf[0] = '0'; ripbuf[1] = 'x';
            for (int k = 15; k >= 0; k--) {
                uint8_t nibble = (r >> (k * 4)) & 0xF;
                ripbuf[17 - k] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
            }
            ripbuf[18] = '\0';
            ai_build_event(evbuf, sizeof(evbuf), vecbuf, ripbuf, (void*)0);
            ai_send_event(AI_EVT_EXCEPTION, evbuf);
            ai_kernel_engine_feed(AI_EVT_EXCEPTION, evbuf, kernel_tick);
        }

        if ((frame->cs & 0x3) == 0x3) {
            // Phase 46: demand paging — check VMAs before killing
            if (vec == 14 && current_task_idx >= 0) {
                uint64_t fault_addr;
                __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
                TaskSlot *t = &task_slots[current_task_idx];
                VmaEntry *vma = vma_find(t->vma_list, fault_addr);
                if (vma) {
                    uint64_t page_va = fault_addr & ~0xFFFULL;
                    uint64_t fr = frame_alloc();
                    if (fr) {
                        uint64_t pg_flags = PAGE_PRESENT | PAGE_USER;
                        if (vma->prot & PROT_WRITE) pg_flags |= PAGE_WRITABLE;

                        if (vma->type == VMA_TYPE_FILE && vma->path[0]) {
                            // File-backed: read file data into the frame
                            uint32_t page_off = (uint32_t)(page_va - vma->start) + vma->file_offset;
                            uint32_t need = page_off + PAGE_SIZE;
                            if (need > sizeof(mmap_file_tmp)) need = sizeof(mmap_file_tmp);
                            int total = kfs_read_file(vma->path, mmap_file_tmp, need);
                            if (total > (int)page_off) {
                                int ncp = total - (int)page_off;
                                if (ncp > PAGE_SIZE) ncp = PAGE_SIZE;
                                uint8_t *dst = (uint8_t *)fr;
                                for (int b = 0; b < ncp; b++)
                                    dst[b] = mmap_file_tmp[page_off + b];
                            }
                        }
                        // Anonymous: frame already zeroed by frame_alloc

                        paging_map_4k(kernel_pml4, page_va, fr, pg_flags);
                        paging_flush_tlb();
                        serial_print("[demand-page] va=0x");
                        serial_print_hex(page_va);
                        serial_print(" -> frame=0x");
                        serial_print_hex(fr);
                        serial_print("\n");
                        return 0;  // Resume user execution
                    }
                }
            }

            // User-mode fault: kill the faulting process and switch to next task
            void *sched = get_kernel_scheduler();
            if (sched) {
                uint32_t pid = scheduler_get_current_pid(sched);
                if (pid != 0) {
                    serial_print("[EXCEPTION] user fault: kill pid=");
                    serial_print_uint(pid);
                    serial_print("\n");
                    (void)scheduler_kill_process(sched, pid);
                    (void)scheduler_schedule(sched);
                }
            }
            // Deactivate the faulting task slot and switch to next via context switch
            if (context_switch_enabled && current_task_idx >= 0) {
                task_slots[current_task_idx].active = 0;
                int next = -1;
                for (int i = 0; i < MAX_TASKS; i++) {
                    int idx = (current_task_idx + 1 + i) % MAX_TASKS;
                    if (task_slots[idx].active) { next = idx; break; }
                }
                if (next >= 0) {
                    serial_print("[EXCEPTION] switching to task ");
                    serial_print_uint(next);
                    serial_print("\n");
                    current_task_idx = next;
                    return task_slots[next].rsp;
                }
            }
            serial_print("[EXCEPTION] no task to switch to; halting\n");
        }
    }

    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
    return 0;  // unreachable
}

// =============================================================================
// MSR Helpers (for Phase 5: SYSCALL/SYSRET)
// =============================================================================

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr"
        : : "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

// =============================================================================
// Phase 5: SYSCALL/SYSRET setup
// =============================================================================

extern void syscall_entry(void);  // Defined in syscall.asm

// Rust syscall_handler (defined in libvernisos_x64.a)
extern int64_t syscall_handler(uint32_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

// C-level syscall handler called from syscall.asm
uint64_t c_syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2,
                            uint64_t arg3, uint64_t arg4) {
    (void)arg4;
    if (num == SYS_READ) {
        return (uint64_t)syscall_vfs_read_64(arg1, arg2, arg3);
    }
    if (num == SYS_WRITE) {
        return (uint64_t)syscall_vfs_write_64(arg1, arg2, arg3);
    }
    // Phase 4: IPC syscalls 20-27 → C handler
    if (num >= SYS_IPC_SEND && num <= SYS_IPC_CHAN_CLOSE) {
        return (uint64_t)ipc_syscall((uint32_t)num, (uint32_t)arg1,
                                      (uint32_t)arg2, (uint32_t)arg3);
    }
    // Phase 5: Module syscalls 28-32 → C handler
    if (num >= SYS_MOD_LOAD && num <= SYS_MOD_INFO) {
        return (uint64_t)module_syscall((uint32_t)num, (uint32_t)arg1,
                                         (uint32_t)arg2, (uint32_t)arg3);
    }
    // Phase 8: AI syscalls 40-42 → C handler
    if (num >= SYS_AI_QUERY && num <= SYS_AI_EVENT) {
        return (uint64_t)ai_syscall((uint32_t)num, (uint32_t)arg1,
                                     (uint32_t)arg2, (uint32_t)arg3);
    }
    // Phase 3: all other syscalls → Rust handler
    return (uint64_t)syscall_handler((uint32_t)num, arg1, arg2, arg3);
}

static void syscall_hw_init(void) {
    // Enable SCE (syscall extensions) in IA32_EFER
    wrmsr(0xC0000080, rdmsr(0xC0000080) | 1);
    // STAR: bits[47:32] = kernel CS (0x08), bits[63:48] = user CS (0x18)
    wrmsr(0xC0000081, ((uint64_t)0x0018 << 48) | ((uint64_t)0x0008 << 32));
    // LSTAR: syscall entry point
    wrmsr(0xC0000082, (uint64_t)syscall_entry);
    // SFMASK: clear IF (bit 9) and TF (bit 8) on syscall entry
    wrmsr(0xC0000084, 0x300);

    serial_print("[syscall] SYSCALL/SYSRET initialized\n");
}

// =============================================================================
// Heap + Rust FFI (Phase 3 + 4)
// =============================================================================

extern void verniskernel_init_heap(uint64_t start, uint64_t size);
extern void verniskernel_register_print(void (*cb)(const uint8_t *, uint32_t));
extern void syscall_init(void);   // Rust syscall_init (no-op stub, symbol needed)
extern int64_t syscall_handler(uint32_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
extern void *scheduler_new(void);
extern uint32_t scheduler_create_process(void *sched, uint8_t priority, const char *command);
extern uint32_t scheduler_schedule(void *sched);
extern uint32_t scheduler_get_process_count(const void *sched);
extern int scheduler_set_priority(void *sched, uint32_t pid, uint8_t priority);
extern void scheduler_set_quantum(void *sched, uint32_t quantum_ms);

static void *kernel_scheduler = (void *)0;
void *get_kernel_scheduler(void) { return kernel_scheduler; }
uint32_t kernel_get_ticks(void)  { return (uint32_t)kernel_tick; }
uint32_t kernel_is_gui_mode(void) { return (display_mode == 2) ? 1u : 0u; }

static void rust_print_cb(const uint8_t *ptr, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) serial_putchar((char)ptr[i]);
}

// Static heap: 8MB, 4KB-aligned (GUI needs back-buffer + window buffers)
#define HEAP_SIZE (8 * 1024 * 1024)
static uint8_t kernel_heap[HEAP_SIZE] __attribute__((aligned(4096)));

// =============================================================================
// Phase 16: Paging — Identity Map + Physical Frame Allocator (x86_64)
// =============================================================================

#ifndef PAGE_SIZE
#define PAGE_SIZE       4096
#endif
#ifndef PAGE_PRESENT
#define PAGE_PRESENT    0x01ULL
#define PAGE_WRITABLE   0x02ULL
#define PAGE_USER       0x04ULL
#define PAGE_PS         0x80ULL   // 2MB (PD) or 1GB (PDPT) huge page
#endif

// Physical frame allocator — simple bump allocator starting after _kernel_end
static uint64_t phys_next_free;
static uint64_t phys_alloc_end;
static uint64_t phys_frames_used;

static uint64_t frame_alloc(void) {
    if (phys_next_free >= phys_alloc_end) {
        serial_print("[paging] FATAL: out of page frames\n");
        return 0;
    }
    uint64_t frame = phys_next_free;
    phys_next_free += PAGE_SIZE;
    phys_frames_used++;
    // Zero the frame (required for page tables)
    volatile uint8_t *p = (volatile uint8_t *)frame;
    for (int i = 0; i < PAGE_SIZE; i++) p[i] = 0;
    return frame;
}

// ---- 4-level page table helpers ----

// Get the next-level table from a given entry, creating it if absent.
// Propagates PAGE_USER into intermediate entries so user-mode pages
// are accessible through all levels of the page-table hierarchy.
static uint64_t *paging_next_level(uint64_t *table, int index, uint64_t flags) {
    if (!(table[index] & PAGE_PRESENT)) {
        uint64_t frame = frame_alloc();
        if (!frame) return (uint64_t *)0;
        table[index] = frame | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        table[index] |= (flags & PAGE_USER);
    }
    return (uint64_t *)(table[index] & 0x000FFFFFFFFFF000ULL);
}

// Map a single 2MB huge page (identity or arbitrary).
static void paging_map_2m(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    int pml4i = (virt >> 39) & 0x1FF;
    int pdpti = (virt >> 30) & 0x1FF;
    int pdi   = (virt >> 21) & 0x1FF;

    uint64_t *pdpt = paging_next_level(pml4, pml4i, flags);
    if (!pdpt) return;
    uint64_t *pd = paging_next_level(pdpt, pdpti, flags);
    if (!pd) return;
    pd[pdi] = (phys & 0xFFFFFFFFFFE00000ULL) | flags | PAGE_PS;
}

// Map a single 4KB page (for user space — Phase 17+).
static void paging_map_4k(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    int pml4i = (virt >> 39) & 0x1FF;
    int pdpti = (virt >> 30) & 0x1FF;
    int pdi   = (virt >> 21) & 0x1FF;
    int pti   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = paging_next_level(pml4, pml4i, flags);
    if (!pdpt) return;
    uint64_t *pd = paging_next_level(pdpt, pdpti, flags);
    if (!pd) return;
    uint64_t *pt = paging_next_level(pd, pdi, flags);
    if (!pt) return;
    pt[pti] = (phys & 0xFFFFFFFFF000ULL) | flags;
}

// Create a new address space for a user process.
// Copies kernel PML4 entries so kernel memory is always mapped.
uint64_t *paging_create_address_space(uint64_t *kernel_pml4) {
    uint64_t frame = frame_alloc();
    if (!frame) return (uint64_t *)0;
    uint64_t *new_pml4 = (uint64_t *)frame;
    // Copy all PML4 entries (kernel mappings)
    for (int i = 0; i < 512; i++) new_pml4[i] = kernel_pml4[i];
    return new_pml4;
}

// Flush entire TLB by rewriting CR3
static void paging_flush_tlb(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

// Global kernel PML4 pointer (set by paging_init)
static uint64_t *kernel_pml4 = (uint64_t *)0;

// Public accessor for Phase 17+ (creating user address spaces)
uint64_t *paging_get_kernel_pml4(void) { return kernel_pml4; }
uint64_t  paging_get_frames_used(void) { return phys_frames_used; }

// Initialize kernel page tables — called from kernel_main AFTER BSS zeroing
// and BEFORE sti.  Replaces the bootloader's minimal page tables.
static void paging_init(void) {
    extern char _kernel_end[];
    // Frame allocator starts right after kernel BSS, aligned to 4KB
    phys_next_free  = ((uint64_t)_kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    phys_alloc_end  = phys_next_free + (4ULL * 1024 * 1024);  // 4MB for page structs
    phys_frames_used = 0;

    // Allocate new PML4
    kernel_pml4 = (uint64_t *)frame_alloc();
    if (!kernel_pml4) { serial_print("[paging] FATAL: cannot alloc PML4\n"); return; }

    // Identity-map first 128MB with 2MB huge pages (covers kernel, heap, stacks)
    for (uint64_t addr = 0; addr < 128ULL * 1024 * 1024; addr += 2ULL * 1024 * 1024) {
        paging_map_2m(kernel_pml4, addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
    }

    // Map framebuffer if present (typically at 0xFD000000, need ~4MB)
    {
        volatile struct boot_info *bi = (volatile struct boot_info *)0x5300;
        if (bi->magic == 0x56424549 && bi->fb_type == 1) {
            uint64_t fb_phys = (uint64_t)bi->fb_addr | ((uint64_t)bi->fb_addr_high << 32);
            uint64_t fb_size = (uint64_t)bi->fb_pitch * bi->fb_height;
            // Round up to 2MB boundary for coverage
            uint64_t fb_base = fb_phys & 0xFFFFFFFFFFE00000ULL;
            uint64_t fb_end  = (fb_phys + fb_size + 0x1FFFFF) & 0xFFFFFFFFFFE00000ULL;
            for (uint64_t a = fb_base; a < fb_end; a += 2ULL * 1024 * 1024) {
                paging_map_2m(kernel_pml4, a, a, PAGE_PRESENT | PAGE_WRITABLE);
            }
            serial_print("[paging] framebuffer mapped ");
            serial_print_hex((uint32_t)fb_base);
            serial_print("-");
            serial_print_hex((uint32_t)fb_end);
            serial_print("\n");
        }
    }

    // Switch to new page tables (TLB flush implicit)
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint64_t)kernel_pml4) : "memory");

    serial_print("[paging] initialized: 128MB identity + fb, ");
    serial_print_dec(phys_frames_used);
    serial_print(" frames used\n");
}

// =============================================================================
// Phase 18: Context Switch — helper functions
// =============================================================================

// Public accessor for CLI "sched" command
uint64_t kernel_get_context_switches(void) { return context_switch_count; }

static int task_find(uint32_t pid) {
    for (int i = 0; i < MAX_TASKS; i++)
        if (task_slots[i].active && task_slots[i].pid == pid) return i;
    return -1;
}

// Create a new kernel-mode task.  Builds a fake InterruptFrame on its stack
// so the first context-switch into it looks identical to a resumed task.
static int task_create(void (*entry)(void), uint32_t pid, uint16_t ticks) {
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++)
        if (!task_slots[i].active) { slot = i; break; }
    if (slot < 0) return -1;

    task_slots[slot].active = 1;
    task_slots[slot].pid    = pid;
    task_slots[slot].ticks_remaining = ticks;
    task_slots[slot].ticks_total     = ticks;
    task_slots[slot].brk             = 0;
    task_slots[slot].mmap_next       = 0;
    vma_init(task_slots[slot].vma_list);

    uint64_t *sp = (uint64_t*)(task_slots[slot].stack + TASK_STACK_SIZE);

    // --- iretq always pops all 5 in long mode ---
    *(--sp) = 0x10;                                                      // SS
    *(--sp) = (uint64_t)(task_slots[slot].stack + TASK_STACK_SIZE);      // RSP
    *(--sp) = 0x202;                                                     // RFLAGS (IF=1)
    *(--sp) = 0x08;                                                      // CS
    *(--sp) = (uint64_t)entry;                                           // RIP

    // --- ISR macro pushes error_code + int_no ---
    *(--sp) = 0;   // error_code
    *(--sp) = 0;   // int_no

    // --- isr_common_stub pushes: rax rcx rdx rbx rbp rsi rdi r8-r15 ---
    for (int i = 0; i < 15; i++) *(--sp) = 0;

    task_slots[slot].rsp = (uint64_t)sp;
    return slot;
}

// Register the already-running main kernel thread (uses the boot stack).
static void task_register_main(uint32_t pid, uint16_t ticks) {
    task_slots[0].active          = 1;
    task_slots[0].pid             = pid;
    task_slots[0].rsp             = 0;           // saved on first preemption
    task_slots[0].ticks_remaining = ticks;
    task_slots[0].ticks_total     = ticks;
    current_task_idx = 0;
}

// Simple background worker — proves context switch works
static void phase18_worker_entry(void) {
    volatile uint64_t local_tick = 0;
    while (1) {
        __asm__ volatile("hlt");
        local_tick++;
        if ((local_tick % TIMER_HZ) == 0) {
            serial_print("[worker] ctx_sw=");
            serial_print_uint(context_switch_count);
            serial_print("\n");
        }
    }
}

// =============================================================================
// Phase 17: Ring 3 — User-mode task creation
// =============================================================================

// User-mode virtual address layout (placed beyond the 128 MB identity region
// so they don't collide with the kernel's 2 MB huge-page mappings).
#define USER_CODE_VADDR  0x10000000ULL   // 256 MB — user .text page
#define USER_STACK_TOP   0x10800000ULL   // 264 MB — top of user stack
#define USER_STACK_SIZE  0x4000ULL       // 16 KB user stack

// Legacy phase17 heartbeat task helpers removed.
// User-space boot path is now shell-only via ELF loader (/bin/vsh64).

// =============================================================================
// Phase 19: ELF Loader
// =============================================================================

// ELF64 header structures (minimal — only what we parse)
#define ELF_MAGIC 0x464C457FU   // "\x7FELF" as little-endian uint32

typedef struct {
    uint32_t e_magic;
    uint8_t  e_class;       // 1=32-bit, 2=64-bit
    uint8_t  e_data;        // 1=LE, 2=BE
    uint8_t  e_version;
    uint8_t  e_osabi;
    uint8_t  e_pad[8];
    uint16_t e_type;        // 2=ET_EXEC
    uint16_t e_machine;     // 0x3E=x86_64
    uint32_t e_version2;
    uint64_t e_entry;
    uint64_t e_phoff;       // program header table offset
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    uint32_t p_type;        // 1=PT_LOAD
    uint32_t p_flags;       // PF_R=4, PF_W=2, PF_X=1
    uint64_t p_offset;      // file offset
    uint64_t p_vaddr;       // virtual address
    uint64_t p_paddr;
    uint64_t p_filesz;      // size in file
    uint64_t p_memsz;       // size in memory (>= filesz for .bss)
    uint64_t p_align;
} __attribute__((packed)) Elf64_Phdr;

#define PT_LOAD  1
#define ET_EXEC  2
#define EM_X86_64 0x3E

// Temporary buffer for reading ELF files from VernisFS (128 KB max)
#define ELF_BUF_SIZE  (128 * 1024)
static uint8_t elf_load_buf[ELF_BUF_SIZE] __attribute__((aligned(4096)));

// Load an ELF64 executable from VernisFS and create a Ring 3 task.
// Returns task slot index on success, or -1 on error.
static int elf_exec(const char *path, uint16_t ticks) {
    // 1. Read file from VernisFS
    int file_size = kfs_read_file(path, elf_load_buf, ELF_BUF_SIZE);
    if (file_size < (int)sizeof(Elf64_Ehdr)) {
        serial_print("[elf] failed to read: ");
        serial_print(path);
        serial_print("\n");
        return -1;
    }

    // 2. Validate ELF header
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_load_buf;
    if (ehdr->e_magic != ELF_MAGIC || ehdr->e_class != 2 ||
        ehdr->e_data != 1 || ehdr->e_machine != EM_X86_64 ||
        ehdr->e_type != ET_EXEC) {
        serial_print("[elf] invalid header: ");
        serial_print(path);
        serial_print("\n");
        return -1;
    }
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        serial_print("[elf] no program headers\n");
        return -1;
    }

    // 3. Allocate a task slot
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++)
        if (!task_slots[i].active) { slot = i; break; }
    if (slot < 0) {
        serial_print("[elf] no free task slot\n");
        return -1;
    }

    // 4. Create a scheduler process
    uint32_t pid = scheduler_create_process(kernel_scheduler, 5, path);

    task_slots[slot].active          = 1;
    task_slots[slot].pid             = pid;
    task_slots[slot].ticks_remaining = ticks;
    task_slots[slot].ticks_total     = ticks;
    task_slots[slot].brk             = 0;
    fd_table_init(task_slots[slot].fd_table);  // Phase 41: init fd 0/1/2
    vma_init(task_slots[slot].vma_list);       // Phase 46: init VMAs
    task_slots[slot].mmap_next       = 0;

    // 5. Map PT_LOAD segments into user space
    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf_load_buf + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_memsz == 0) continue;

        uint64_t seg_vaddr  = phdr[i].p_vaddr;
        uint64_t seg_filesz = phdr[i].p_filesz;
        uint64_t seg_memsz  = phdr[i].p_memsz;
        uint64_t seg_foff   = phdr[i].p_offset;

        uint64_t vaddr = seg_vaddr & ~(PAGE_SIZE - 1ULL);
        uint64_t vend  = (seg_vaddr + seg_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1ULL);

        // Map each page
        for (uint64_t va = vaddr; va < vend; va += PAGE_SIZE) {
            uint64_t frame = frame_alloc();
            if (!frame) {
                serial_print("[elf] out of frames\n");
                task_slots[slot].active = 0;
                return -1;
            }

            // Copy file data byte-by-byte within this page
            // For each byte at virtual address (va + b):
            //   seg_offset = (va + b) - seg_vaddr
            //   if seg_offset < seg_filesz → copy from file at seg_foff + seg_offset
            //   else → zero (already zeroed by frame_alloc)
            for (int b = 0; b < PAGE_SIZE; b++) {
                uint64_t abs_va = va + (uint64_t)b;
                if (abs_va < seg_vaddr || abs_va >= seg_vaddr + seg_memsz)
                    continue;  // outside segment → stays zero
                uint64_t seg_off = abs_va - seg_vaddr;
                if (seg_off < seg_filesz) {
                    uint64_t foff = seg_foff + seg_off;
                    if (foff < (uint64_t)file_size)
                        ((uint8_t *)frame)[b] = elf_load_buf[foff];
                }
            }

            paging_map_4k(kernel_pml4, va, frame,
                          PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
    }

    // 6. Map user stack (16 KB = 4 pages)
    for (uint64_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
        uint64_t frame = frame_alloc();
        if (!frame) {
            serial_print("[elf] out of frames for stack\n");
            task_slots[slot].active = 0;
            return -1;
        }
        paging_map_4k(kernel_pml4, USER_STACK_TOP - USER_STACK_SIZE + i, frame,
                      PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    paging_flush_tlb();

    // 7. Build iretq frame on the kernel stack
    uint64_t *sp = (uint64_t*)(task_slots[slot].stack + TASK_STACK_SIZE);

    *(--sp) = 0x20 | 3;                          // SS  (0x23)
    *(--sp) = USER_STACK_TOP;                     // RSP
    *(--sp) = 0x202;                              // RFLAGS (IF=1)
    *(--sp) = 0x18 | 3;                           // CS  (0x1B)
    *(--sp) = ehdr->e_entry;                      // RIP = ELF entry point

    *(--sp) = 0;   // error_code
    *(--sp) = 0;   // int_no

    for (int g = 0; g < 15; g++) *(--sp) = 0;    // 15 GPRs

    task_slots[slot].rsp = (uint64_t)sp;

    serial_print("[elf] loaded ");
    serial_print(path);
    serial_print(" entry=0x");
    // Print entry address in hex
    {
        uint64_t v = ehdr->e_entry;
        char hx[17]; hx[16] = 0;
        for (int d = 15; d >= 0; d--) {
            uint8_t nib = v & 0xF;
            hx[d] = nib < 10 ? '0' + nib : 'a' + nib - 10;
            v >>= 4;
        }
        serial_print(hx);
    }
    serial_print(" pid=");
    serial_print_dec(pid);
    serial_print(" slot=");
    serial_print_dec(slot);
    serial_print("\n");

    return slot;
}

// Kernel-callable function for CLI "exec" command
// Returns 0 on success, -1 on failure
static int kernel_elf_exec(const char *path) {
    int slot = elf_exec(path, 24);
    if (slot >= 0) {
        kernel_tss.rsp[0] = (uint64_t)(task_slots[slot].stack + TASK_STACK_SIZE);
    }
    return slot >= 0 ? 0 : -1;
}

// Global function pointer for CLI to call
int (*g_elf_exec_fn)(const char *path) = (void *)0;

// =============================================================================
// Phase 11: AI Auto-Tuner decision handler
// =============================================================================

static void kernel_tune_handler(const char *action, const char *target,
                                 uint32_t value, const char *reason) {
    (void)target; (void)reason;
    if (!kernel_scheduler) return;
    if (action[0]=='S' && action[1]=='C' && action[6]=='Q') {
        if (value < 1) value = 1;
        if (value > 200) value = 200;
        scheduler_set_quantum(kernel_scheduler, value);
        return;
    }
    if (action[0]=='S' && action[1]=='C' && action[6]=='P') {
        scheduler_set_priority(kernel_scheduler, 1, (uint8_t)value);
        return;
    }
}

// Phase 10: AI Remediation handler — responds to CMD|REMEDIATE from Python
static void kernel_remediate_handler(const char *action, const char *target,
                                      const char *param) {
    serial_print("[ai-remediate] action=");
    serial_print(action);
    serial_print(" target=");
    serial_print(target);
    serial_print(" param=");
    serial_print(param ? param : "none");
    serial_print("\n");

    if (!kernel_scheduler) return;

    // Parse target PID
    uint32_t pid = 0;
    if (target[0] != 'a') {  // not "all"
        for (const char *d = target; *d >= '0' && *d <= '9'; d++)
            pid = pid * 10 + (uint32_t)(*d - '0');
    }

    // "throttle" → reduce scheduler quantum for target
    if (action[0] == 't' && action[1] == 'h') {
        uint32_t quantum = 0;
        if (param) {
            for (const char *d = param; *d >= '0' && *d <= '9'; d++)
                quantum = quantum * 10 + (uint32_t)(*d - '0');
        }
        if (quantum < 1) quantum = 10;
        if (quantum > 200) quantum = 200;
        scheduler_set_quantum(kernel_scheduler, quantum);
        serial_print("[ai-remediate] throttled quantum=");
        serial_print_dec(quantum);
        serial_print("ms\n");
        return;
    }

    // "kill" → set process priority to 0 (effectively dead)
    if (action[0] == 'k' && action[1] == 'i') {
        if (pid > 0) {
            scheduler_set_priority(kernel_scheduler, pid, 0);
            serial_print("[ai-remediate] killed pid=");
            serial_print_dec(pid);
            serial_print("\n");
        }
        return;
    }

    // "log" → just log (already printed above)
    // "suspend", "revoke" → log for now (full implementation in Phase 13)
}

// =============================================================================
// Deferred AI engine work — called from CLI idle loop (main thread context)
// Safe for heap allocations; timer IRQ only sets kernel_tick.
// =============================================================================

static uint64_t g_ai_last_tick  = 0;
static uint64_t g_ai_last_stat  = 0;

void kernel_idle_work(void) {
    uint64_t now = kernel_tick;
    const uint32_t ai_tick_interval = (TIMER_HZ / 2) ? (TIMER_HZ / 2) : 1; // ~500ms
    const uint32_t ai_stat_interval = TIMER_HZ ? TIMER_HZ : 1;              // ~1s

    // Every ~500ms: tick the in-kernel AI engine
    if (now - g_ai_last_tick >= ai_tick_interval) {
        g_ai_last_tick = now;
        ai_kernel_engine_tick(now);
    }

    // Every ~1s: send STAT events
    if (now - g_ai_last_stat >= ai_stat_interval) {
        g_ai_last_stat = now;
        void *sched = get_kernel_scheduler();
        if (sched) {
            uint32_t procs = scheduler_get_process_count(sched);
            char buf[24];
            const char *key = "process_count|";
            char *p = buf;
            for (const char *k = key; *k; k++) *p++ = *k;
            uint32_t v = procs; int i = 0; char tmp[12];
            if (v == 0) { tmp[i++] = '0'; }
            else { while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; } }
            for (int j = i - 1; j >= 0; j--) *p++ = tmp[j];
            *p = '\0';
            ai_send_event("STAT", buf);
            ai_kernel_engine_feed("STAT", buf, now);
        }
    }
}

// =============================================================================
// GUI ↔ CLI bridge
// =============================================================================

static CliShell *gui_shell = 0;
static CliSession *gui_session = 0;

// Called from Rust gui::terminal when user presses Enter
void cli_process_line_gui(const uint8_t *line, uint32_t len) {
    if (!gui_shell || !gui_session || !line || len == 0) return;
    // Copy to null-terminated buffer
    char buf[256];
    uint32_t copy_len = len < 255 ? len : 255;
    for (uint32_t i = 0; i < copy_len; i++) buf[i] = (char)line[i];
    buf[copy_len] = '\0';
    cli_process_line(gui_shell, gui_session, buf);
}

// =============================================================================
// Phase 22: PCI Bus Enumeration
// =============================================================================

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

typedef struct {
    uint8_t  bus, slot, func;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass;
    uint32_t bar0;
    uint8_t  irq;
} PciDev;

#define MAX_PCI_DEVS 32
static PciDev pci_devs[MAX_PCI_DEVS];
static int    pci_dev_count = 0;

static void pci_scan(void) {
    pci_dev_count = 0;
    for (int bus = 0; bus < 256 && pci_dev_count < MAX_PCI_DEVS; bus++) {
        for (int slot = 0; slot < 32 && pci_dev_count < MAX_PCI_DEVS; slot++) {
            uint32_t id = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0);
            uint16_t vendor = id & 0xFFFF;
            if (vendor == 0xFFFF) continue;
            uint16_t device = id >> 16;
            uint32_t cls = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0x08);
            uint32_t bar0 = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0x10);
            uint32_t irq_info = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0x3C);
            PciDev *d = &pci_devs[pci_dev_count++];
            d->bus = (uint8_t)bus; d->slot = (uint8_t)slot; d->func = 0;
            d->vendor_id = vendor; d->device_id = device;
            d->class_code = (cls >> 24) & 0xFF;
            d->subclass = (cls >> 16) & 0xFF;
            d->bar0 = bar0;
            d->irq = irq_info & 0xFF;
        }
    }
}

// Exports for CLI
int kernel_pci_count(void) { return pci_dev_count; }
void kernel_pci_get(int idx, uint16_t *vendor, uint16_t *device,
                    uint8_t *cls, uint8_t *sub, uint8_t *bus, uint8_t *slot) {
    if (idx < 0 || idx >= pci_dev_count) return;
    PciDev *d = &pci_devs[idx];
    *vendor = d->vendor_id; *device = d->device_id;
    *cls = d->class_code; *sub = d->subclass;
    *bus = d->bus; *slot = d->slot;
}

// =============================================================================
// Phase 32: AHCI (foundation probe + ABAR mapping)
// =============================================================================

static volatile uint8_t *ahci_mmio = 0;
static uint32_t ahci_cap = 0;
static uint32_t ahci_slots = 0;
static uint32_t ahci_pi = 0;
static uint32_t ahci_vs = 0;
static int ahci_ports = 0;
static int ahci_up = 0;

#define AHCI_MAX_PORTS 32
#define AHCI_MAX_SLOTS 32

#define AHCI_SIG_ATA   0x00000101u
#define AHCI_SIG_ATAPI 0xEB140101u
#define AHCI_SIG_SEMB  0xC33C0101u
#define AHCI_SIG_PM    0x96690101u

typedef struct {
    uint8_t  cfl;
    uint8_t  flags;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t res[4];
} __attribute__((packed)) AhciCmdHeader;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t res0;
    uint32_t dbc_i;
} __attribute__((packed)) AhciPrdtEntry;

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t res[48];
    AhciPrdtEntry prdt[1];
} __attribute__((packed)) AhciCmdTable1;

static AhciCmdHeader ahci_cmd_headers[AHCI_MAX_PORTS][AHCI_MAX_SLOTS] __attribute__((aligned(1024)));
static AhciCmdTable1 ahci_cmd_tables[AHCI_MAX_PORTS][AHCI_MAX_SLOTS] __attribute__((aligned(128)));
static uint8_t ahci_rfis[AHCI_MAX_PORTS][256] __attribute__((aligned(256)));
static uint16_t ahci_ident_data[AHCI_MAX_PORTS][256] __attribute__((aligned(512)));
static uint8_t ahci_io_buf[AHCI_MAX_PORTS][4096] __attribute__((aligned(512)));
static uint8_t ahci_ident_ok[AHCI_MAX_PORTS];
static char ahci_ident_model[AHCI_MAX_PORTS][41];

static int ahci_port_connected(uint32_t ssts);

static int ahci_count_bits(uint32_t v) {
    int c = 0;
    while (v) {
        c += (int)(v & 1u);
        v >>= 1;
    }
    return c;
}

static void ahci_memzero(void *ptr, uint32_t n) {
    uint8_t *p = (uint8_t *)ptr;
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
}

static void ahci_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void ahci_extract_model(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) return;
    char *dst = ahci_ident_model[port];
    const uint16_t *id = ahci_ident_data[port];
    for (int i = 0; i < 20; i++) {
        uint16_t w = id[27 + i];
        dst[i * 2] = (char)((w >> 8) & 0xFF);
        dst[i * 2 + 1] = (char)(w & 0xFF);
    }
    dst[40] = '\0';
    for (int i = 0; i < 40; i++) {
        if ((unsigned char)dst[i] < 32 || (unsigned char)dst[i] > 126) dst[i] = ' ';
    }
    for (int i = 39; i >= 0; i--) {
        if (dst[i] == ' ') dst[i] = '\0';
        else break;
    }
}

static volatile uint8_t *ahci_port_base(int port) {
    if (!ahci_mmio || port < 0 || port >= 32) return 0;
    return ahci_mmio + 0x100 + ((uint32_t)port * 0x80);
}

static int ahci_port_sig_type(uint32_t sig, uint32_t ssts) {
    if (!ahci_port_connected(ssts)) return 0;
    if (sig == AHCI_SIG_ATA) return 1;
    if (sig == AHCI_SIG_ATAPI) return 2;
    if (sig == AHCI_SIG_SEMB) return 3;
    if (sig == AHCI_SIG_PM) return 4;
    return 5;
}

static void ahci_port_stop(int port) {
    volatile uint8_t *pb = ahci_port_base(port);
    if (!pb) return;
    volatile uint32_t *pcmd = (volatile uint32_t *)(pb + 0x18);
    uint32_t v = *pcmd;
    v &= ~1u;
    v &= ~(1u << 4);
    *pcmd = v;
    for (int i = 0; i < 1000000; i++) {
        uint32_t cmd = *pcmd;
        if (!(cmd & (1u << 15)) && !(cmd & (1u << 14))) break;
    }
}

static void ahci_port_start(int port) {
    volatile uint8_t *pb = ahci_port_base(port);
    if (!pb) return;
    volatile uint32_t *pcmd = (volatile uint32_t *)(pb + 0x18);
    for (int i = 0; i < 1000000; i++) {
        if (!((*pcmd) & (1u << 15))) break;
    }
    *pcmd |= (1u << 4);
    *pcmd |= 1u;
}

static int ahci_find_slot(int port) {
    volatile uint8_t *pb = ahci_port_base(port);
    if (!pb) return -1;
    uint32_t slots = *(volatile uint32_t *)(pb + 0x34) | *(volatile uint32_t *)(pb + 0x38);
    uint32_t limit = ahci_slots;
    if (limit == 0 || limit > AHCI_MAX_SLOTS) limit = AHCI_MAX_SLOTS;
    for (uint32_t i = 0; i < limit; i++) {
        if (!(slots & (1u << i))) return (int)i;
    }
    return -1;
}

static int ahci_port_rebase(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) return -1;
    volatile uint8_t *pb = ahci_port_base(port);
    if (!pb) return -1;
    ahci_port_stop(port);

    uintptr_t clb = (uintptr_t)&ahci_cmd_headers[port][0];
    uintptr_t fb = (uintptr_t)&ahci_rfis[port][0];
    *(volatile uint32_t *)(pb + 0x00) = (uint32_t)clb;
    *(volatile uint32_t *)(pb + 0x04) = (uint32_t)(clb >> 32);
    *(volatile uint32_t *)(pb + 0x08) = (uint32_t)fb;
    *(volatile uint32_t *)(pb + 0x0C) = (uint32_t)(fb >> 32);

    ahci_memzero(&ahci_cmd_headers[port][0], sizeof(ahci_cmd_headers[port]));
    ahci_memzero(&ahci_rfis[port][0], sizeof(ahci_rfis[port]));

    for (int i = 0; i < AHCI_MAX_SLOTS; i++) {
        AhciCmdHeader *h = &ahci_cmd_headers[port][i];
        uintptr_t ctba = (uintptr_t)&ahci_cmd_tables[port][i];
        h->ctba = (uint32_t)ctba;
        h->ctbau = (uint32_t)(ctba >> 32);
        h->prdtl = 0;
        h->prdbc = 0;
        ahci_memzero(&ahci_cmd_tables[port][i], sizeof(AhciCmdTable1));
    }

    *(volatile uint32_t *)(pb + 0x10) = 0xFFFFFFFFu;
    *(volatile uint32_t *)(pb + 0x38) = 0;
    ahci_port_start(port);
    return 0;
}

int kernel_ahci_identify(int port) {
    if (!ahci_up || port < 0 || port >= AHCI_MAX_PORTS) return -1;
    if (!(ahci_pi & (1u << (uint32_t)port))) return -1;

    volatile uint8_t *pb = ahci_port_base(port);
    if (!pb) return -1;

    uint32_t ssts = *(volatile uint32_t *)(pb + 0x28);
    uint32_t sig = *(volatile uint32_t *)(pb + 0x24);
    if (!ahci_port_connected(ssts)) return -2;
    if (sig != AHCI_SIG_ATA) return -3;

    if (ahci_port_rebase(port) != 0) return -4;
    int slot = ahci_find_slot(port);
    if (slot < 0) return -5;

    AhciCmdHeader *h = &ahci_cmd_headers[port][slot];
    AhciCmdTable1 *t = &ahci_cmd_tables[port][slot];
    ahci_memzero(h, sizeof(AhciCmdHeader));
    ahci_memzero(t, sizeof(AhciCmdTable1));

    h->cfl = 5;
    h->flags = 0;
    h->prdtl = 1;

    uintptr_t idbuf = (uintptr_t)&ahci_ident_data[port][0];
    t->prdt[0].dba = (uint32_t)idbuf;
    t->prdt[0].dbau = (uint32_t)(idbuf >> 32);
    t->prdt[0].dbc_i = (512u - 1u);

    t->cfis[0] = 0x27;
    t->cfis[1] = (1u << 7);
    t->cfis[2] = 0xEC;

    for (int spin = 0; spin < 1000000; spin++) {
        uint32_t tfd = *(volatile uint32_t *)(pb + 0x20);
        if (!(tfd & (0x80u | 0x08u))) break;
        if (spin == 999999) return -6;
    }

    *(volatile uint32_t *)(pb + 0x10) = 0xFFFFFFFFu;
    *(volatile uint32_t *)(pb + 0x38) = (1u << (uint32_t)slot);

    for (int spin = 0; spin < 4000000; spin++) {
        uint32_t ci = *(volatile uint32_t *)(pb + 0x38);
        uint32_t isr = *(volatile uint32_t *)(pb + 0x10);
        if (!(ci & (1u << (uint32_t)slot))) {
            if (isr & (1u << 30)) return -7;
            ahci_ident_ok[port] = 1;
            ahci_extract_model(port);
            return 0;
        }
    }
    return -8;
}

int kernel_ahci_read(int port, uint64_t lba, uint32_t sectors, uint8_t *out, uint32_t out_max) {
    if (!ahci_up || port < 0 || port >= AHCI_MAX_PORTS) return -1;
    if (!(ahci_pi & (1u << (uint32_t)port))) return -1;
    if (sectors == 0 || sectors > 8) return -9;
    uint32_t bytes = sectors * 512u;
    if (!out || out_max < bytes) return -10;

    volatile uint8_t *pb = ahci_port_base(port);
    if (!pb) return -1;

    uint32_t ssts = *(volatile uint32_t *)(pb + 0x28);
    uint32_t sig = *(volatile uint32_t *)(pb + 0x24);
    if (!ahci_port_connected(ssts)) return -2;
    if (sig != AHCI_SIG_ATA) return -3;

    if (ahci_port_rebase(port) != 0) return -4;
    int slot = ahci_find_slot(port);
    if (slot < 0) return -5;

    AhciCmdHeader *h = &ahci_cmd_headers[port][slot];
    AhciCmdTable1 *t = &ahci_cmd_tables[port][slot];
    ahci_memzero(h, sizeof(AhciCmdHeader));
    ahci_memzero(t, sizeof(AhciCmdTable1));

    h->cfl = 5;
    h->flags = 0;
    h->prdtl = 1;

    uintptr_t dbuf = (uintptr_t)&ahci_io_buf[port][0];
    t->prdt[0].dba = (uint32_t)dbuf;
    t->prdt[0].dbau = (uint32_t)(dbuf >> 32);
    t->prdt[0].dbc_i = (bytes - 1u);

    t->cfis[0] = 0x27;
    t->cfis[1] = (1u << 7);
    t->cfis[2] = 0x25; // READ DMA EXT
    t->cfis[4] = (uint8_t)(lba & 0xFFu);
    t->cfis[5] = (uint8_t)((lba >> 8) & 0xFFu);
    t->cfis[6] = (uint8_t)((lba >> 16) & 0xFFu);
    t->cfis[7] = (1u << 6);
    t->cfis[8] = (uint8_t)((lba >> 24) & 0xFFu);
    t->cfis[9] = (uint8_t)((lba >> 32) & 0xFFu);
    t->cfis[10] = (uint8_t)((lba >> 40) & 0xFFu);
    t->cfis[12] = (uint8_t)(sectors & 0xFFu);
    t->cfis[13] = (uint8_t)((sectors >> 8) & 0xFFu);

    for (int spin = 0; spin < 1000000; spin++) {
        uint32_t tfd = *(volatile uint32_t *)(pb + 0x20);
        if (!(tfd & (0x80u | 0x08u))) break;
        if (spin == 999999) return -6;
    }

    *(volatile uint32_t *)(pb + 0x10) = 0xFFFFFFFFu;
    *(volatile uint32_t *)(pb + 0x38) = (1u << (uint32_t)slot);

    for (int spin = 0; spin < 4000000; spin++) {
        uint32_t ci = *(volatile uint32_t *)(pb + 0x38);
        uint32_t isr = *(volatile uint32_t *)(pb + 0x10);
        if (!(ci & (1u << (uint32_t)slot))) {
            if (isr & (1u << 30)) return -7;
            ahci_memcpy(out, &ahci_io_buf[port][0], bytes);
            return (int)bytes;
        }
    }
    return -8;
}

int kernel_ahci_write(int port, uint64_t lba, uint32_t sectors, const uint8_t *data, uint32_t data_len) {
    if (!ahci_up || port < 0 || port >= AHCI_MAX_PORTS) return -1;
    if (!(ahci_pi & (1u << (uint32_t)port))) return -1;
    if (sectors == 0 || sectors > 8) return -9;
    uint32_t bytes = sectors * 512u;
    if (!data || data_len < bytes) return -10;

    volatile uint8_t *pb = ahci_port_base(port);
    if (!pb) return -1;

    uint32_t ssts = *(volatile uint32_t *)(pb + 0x28);
    uint32_t sig = *(volatile uint32_t *)(pb + 0x24);
    if (!ahci_port_connected(ssts)) return -2;
    if (sig != AHCI_SIG_ATA) return -3;

    if (ahci_port_rebase(port) != 0) return -4;
    int slot = ahci_find_slot(port);
    if (slot < 0) return -5;

    /* copy caller data into DMA buffer */
    ahci_memcpy(&ahci_io_buf[port][0], data, bytes);

    AhciCmdHeader *h = &ahci_cmd_headers[port][slot];
    AhciCmdTable1 *t = &ahci_cmd_tables[port][slot];
    ahci_memzero(h, sizeof(AhciCmdHeader));
    ahci_memzero(t, sizeof(AhciCmdTable1));

    h->cfl = 5 | (1u << 6); /* CFL=5, W=1 (host-to-device) */
    h->flags = 0;
    h->prdtl = 1;

    uintptr_t dbuf = (uintptr_t)&ahci_io_buf[port][0];
    t->prdt[0].dba = (uint32_t)dbuf;
    t->prdt[0].dbau = (uint32_t)(dbuf >> 32);
    t->prdt[0].dbc_i = (bytes - 1u);

    t->cfis[0] = 0x27;
    t->cfis[1] = (1u << 7);
    t->cfis[2] = 0x35; /* WRITE DMA EXT */
    t->cfis[4] = (uint8_t)(lba & 0xFFu);
    t->cfis[5] = (uint8_t)((lba >> 8) & 0xFFu);
    t->cfis[6] = (uint8_t)((lba >> 16) & 0xFFu);
    t->cfis[7] = (1u << 6);
    t->cfis[8] = (uint8_t)((lba >> 24) & 0xFFu);
    t->cfis[9] = (uint8_t)((lba >> 32) & 0xFFu);
    t->cfis[10] = (uint8_t)((lba >> 40) & 0xFFu);
    t->cfis[12] = (uint8_t)(sectors & 0xFFu);
    t->cfis[13] = (uint8_t)((sectors >> 8) & 0xFFu);

    for (int spin = 0; spin < 1000000; spin++) {
        uint32_t tfd = *(volatile uint32_t *)(pb + 0x20);
        if (!(tfd & (0x80u | 0x08u))) break;
        if (spin == 999999) return -6;
    }

    *(volatile uint32_t *)(pb + 0x10) = 0xFFFFFFFFu;
    *(volatile uint32_t *)(pb + 0x38) = (1u << (uint32_t)slot);

    for (int spin = 0; spin < 4000000; spin++) {
        uint32_t ci = *(volatile uint32_t *)(pb + 0x38);
        uint32_t isr = *(volatile uint32_t *)(pb + 0x10);
        if (!(ci & (1u << (uint32_t)slot))) {
            if (isr & (1u << 30)) return -7;
            return (int)bytes;
        }
    }
    return -8;
}

int kernel_ahci_identified(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) return 0;
    return ahci_ident_ok[port] ? 1 : 0;
}

const char *kernel_ahci_model(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) return "";
    return ahci_ident_model[port];
}

static int ahci_port_connected(uint32_t ssts) {
    uint32_t det = ssts & 0x0Fu;
    uint32_t ipm = (ssts >> 8) & 0x0Fu;
    return (det == 3u && ipm == 1u);
}

static int ahci_init(PciDev *dev) {
    uint32_t cmd = pci_read32(dev->bus, dev->slot, 0, 0x04);
    cmd |= (1u << 2) | (1u << 1);
    pci_write32(dev->bus, dev->slot, 0, 0x04, cmd);

    uint32_t bar5 = pci_read32(dev->bus, dev->slot, 0, 0x24);
    if (bar5 & 1u) return -1;

    uint64_t abar_phys = (uint64_t)(bar5 & ~0xFULL);
    if ((bar5 & 0x6) == 0x4) {
        uint32_t bar5_hi = pci_read32(dev->bus, dev->slot, 0, 0x28);
        abar_phys |= ((uint64_t)bar5_hi << 32);
    }
    if (abar_phys == 0) return -1;

    for (uint64_t off = 0; off < 0x2000; off += PAGE_SIZE) {
        paging_map_4k(kernel_pml4, abar_phys + off, abar_phys + off,
                      PAGE_PRESENT | PAGE_WRITABLE);
    }
    paging_flush_tlb();

    ahci_mmio = (volatile uint8_t *)(uintptr_t)abar_phys;
    *(volatile uint32_t *)(ahci_mmio + 0x04) |= (1u << 31); // GHC.AE

    ahci_cap = *(volatile uint32_t *)(ahci_mmio + 0x00);
    ahci_slots = ((ahci_cap >> 8) & 0x1Fu) + 1u;
    ahci_pi = *(volatile uint32_t *)(ahci_mmio + 0x0C);
    ahci_vs = *(volatile uint32_t *)(ahci_mmio + 0x10);
    ahci_ports = ahci_count_bits(ahci_pi);
    ahci_up = 1;
    return 0;
}

int kernel_ahci_available(void) { return ahci_up; }
int kernel_ahci_ports(void) { return ahci_ports; }
uint32_t kernel_ahci_pi(void) { return ahci_pi; }
uint32_t kernel_ahci_version(void) { return ahci_vs; }
int kernel_ahci_port_info(int port, uint32_t *ssts, uint32_t *sig,
                          uint32_t *cmd, uint32_t *tfd, uint32_t *isr) {
    if (!ahci_up || port < 0 || port >= 32) return -1;
    if (!(ahci_pi & (1u << (uint32_t)port))) return -1;
    volatile uint8_t *pb = ahci_port_base(port);
    if (!pb) return -1;
    if (isr)  *isr  = *(volatile uint32_t *)(pb + 0x10);
    if (cmd)  *cmd  = *(volatile uint32_t *)(pb + 0x18);
    if (tfd)  *tfd  = *(volatile uint32_t *)(pb + 0x20);
    if (sig)  *sig  = *(volatile uint32_t *)(pb + 0x24);
    if (ssts) *ssts = *(volatile uint32_t *)(pb + 0x28);
    return 0;
}

// =============================================================================
// Phase 38: NVMe Foundation (PCI detect + BAR0 map + admin queues + identify)
// =============================================================================

static volatile uint8_t *nvme_mmio = 0;
static uint64_t nvme_cap = 0;
static uint32_t nvme_vs = 0;
static uint32_t nvme_mqes = 0;       /* max queue entries supported (0-based) */
static int nvme_up = 0;

/* Admin queue depth — keep small for foundation */
#define NVME_AQ_DEPTH 16

/* NVMe Submission Queue Entry (64 bytes) */
typedef struct {
    uint32_t cdw0;    /* opcode[7:0], fuse[9:8], rsvd[13:10], psdt[15:14], cid[31:16] */
    uint32_t nsid;
    uint64_t rsvd;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) NvmeSqe;

/* NVMe Completion Queue Entry (16 bytes) */
typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;   /* phase bit is bit 0 of status */
} __attribute__((packed)) NvmeCqe;

/* Static queue + identify buffers */
static NvmeSqe  nvme_asq[NVME_AQ_DEPTH] __attribute__((aligned(4096)));
static NvmeCqe  nvme_acq[NVME_AQ_DEPTH] __attribute__((aligned(4096)));
static uint8_t  nvme_ident_buf[4096]     __attribute__((aligned(4096)));

static uint16_t nvme_asq_tail = 0;
static uint16_t nvme_acq_head = 0;
static uint8_t  nvme_acq_phase = 1;
static uint16_t nvme_cid_counter = 0;

static char nvme_model[41];
static char nvme_serial[21];
static int  nvme_ident_ok = 0;

static int nvme_setup_io_queues(void);

static void nvme_memzero(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void nvme_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

/* Read/write NVMe MMIO registers */
static uint32_t nvme_read32(uint32_t off) {
    return *(volatile uint32_t *)(nvme_mmio + off);
}
static void nvme_write32(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(nvme_mmio + off) = val;
}
static uint64_t nvme_read64(uint32_t off) {
    uint32_t lo = *(volatile uint32_t *)(nvme_mmio + off);
    uint32_t hi = *(volatile uint32_t *)(nvme_mmio + off + 4);
    return ((uint64_t)hi << 32) | lo;
}
static void nvme_write64(uint32_t off, uint64_t val) {
    *(volatile uint32_t *)(nvme_mmio + off) = (uint32_t)val;
    *(volatile uint32_t *)(nvme_mmio + off + 4) = (uint32_t)(val >> 32);
}

/* Ring admin SQ doorbell (stride from CAP.DSTRD) */
static uint32_t nvme_db_stride = 4; /* bytes per doorbell register = 4 << DSTRD */

static void nvme_ring_asq(void) {
    /* SQ 0 tail doorbell at offset 0x1000 + (2*0) * nvme_db_stride */
    nvme_write32(0x1000, nvme_asq_tail);
}

/* Wait for admin completion */
static int nvme_wait_acq(uint16_t cid, uint32_t *result) {
    for (int spin = 0; spin < 4000000; spin++) {
        volatile NvmeCqe *cqe = &nvme_acq[nvme_acq_head];
        uint8_t p = (uint8_t)(cqe->status & 1u);
        if (p == nvme_acq_phase) {
            /* Got completion */
            uint16_t got_cid = cqe->cid;
            uint16_t sc = (cqe->status >> 1) & 0x7FFu;
            if (result) *result = cqe->dw0;

            /* Advance head */
            nvme_acq_head++;
            if (nvme_acq_head >= NVME_AQ_DEPTH) {
                nvme_acq_head = 0;
                nvme_acq_phase = (uint8_t)(nvme_acq_phase ^ 1u);
            }
            /* Ring CQ 0 head doorbell at 0x1000 + (2*0+1)*stride */
            nvme_write32(0x1000 + nvme_db_stride, nvme_acq_head);

            if (got_cid != cid) continue; /* not our CQE — try next */
            return (sc == 0) ? 0 : -(int)sc;
        }
    }
    return -999; /* timeout */
}

/* Submit Identify Controller (opcode 0x06, CNS=1) */
static int nvme_identify_controller(void) {
    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_asq_tail;

    nvme_memzero(&nvme_asq[slot], sizeof(NvmeSqe));
    nvme_asq[slot].cdw0 = 0x06u | ((uint32_t)cid << 16); /* opcode=Identify, CID */
    nvme_asq[slot].nsid = 0;
    nvme_asq[slot].prp1 = (uint64_t)(uintptr_t)&nvme_ident_buf[0];
    nvme_asq[slot].prp2 = 0;
    nvme_asq[slot].cdw10 = 1;  /* CNS=1 → Identify Controller */

    nvme_asq_tail++;
    if (nvme_asq_tail >= NVME_AQ_DEPTH) nvme_asq_tail = 0;
    nvme_ring_asq();

    return nvme_wait_acq(cid, 0);
}

/* Extract ASCII string from identify data (space-padded, needs trim) */
static void nvme_extract_str(const uint8_t *src, int len, char *dst, int dmax) {
    int i;
    int copy = len < dmax - 1 ? len : dmax - 1;
    for (i = 0; i < copy; i++) dst[i] = (char)src[i];
    dst[i] = '\0';
    /* trim trailing spaces */
    while (i > 0 && dst[i - 1] == ' ') { i--; dst[i] = '\0'; }
}

static int nvme_init(PciDev *dev) {
    /* Enable bus master + memory access */
    uint32_t cmd = pci_read32(dev->bus, dev->slot, 0, 0x04);
    cmd |= (1u << 2) | (1u << 1);
    pci_write32(dev->bus, dev->slot, 0, 0x04, cmd);

    /* Read BAR0 (MMIO) */
    uint32_t bar0 = pci_read32(dev->bus, dev->slot, 0, 0x10);
    if (bar0 & 1u) return -1; /* must be MMIO */

    uint64_t bar0_phys = (uint64_t)(bar0 & ~0xFULL);
    if ((bar0 & 0x6) == 0x4) {
        uint32_t bar1 = pci_read32(dev->bus, dev->slot, 0, 0x14);
        bar0_phys |= ((uint64_t)bar1 << 32);
    }
    if (bar0_phys == 0) return -1;

    /* Map MMIO (at least 16KB for registers + doorbells) */
    for (uint64_t off = 0; off < 0x4000; off += PAGE_SIZE) {
        paging_map_4k(kernel_pml4, bar0_phys + off, bar0_phys + off,
                      PAGE_PRESENT | PAGE_WRITABLE);
    }
    paging_flush_tlb();

    nvme_mmio = (volatile uint8_t *)(uintptr_t)bar0_phys;

    /* Read CAP, VS */
    nvme_cap = nvme_read64(0x00);
    nvme_vs  = nvme_read32(0x08);
    nvme_mqes = (uint32_t)(nvme_cap & 0xFFFFu);   /* CAP.MQES (0-based) */
    uint32_t dstrd = (uint32_t)((nvme_cap >> 32) & 0xFu);
    nvme_db_stride = 4u << dstrd;

    /* Check CC.EN — disable controller if enabled */
    uint32_t cc = nvme_read32(0x14);
    if (cc & 1u) {
        nvme_write32(0x14, cc & ~1u); /* CC.EN = 0 */
        for (int spin = 0; spin < 2000000; spin++) {
            uint32_t csts = nvme_read32(0x1C);
            if (!(csts & 1u)) break;  /* CSTS.RDY = 0 */
        }
    }

    /* Clear queues */
    nvme_memzero(nvme_asq, sizeof(nvme_asq));
    nvme_memzero(nvme_acq, sizeof(nvme_acq));
    nvme_asq_tail = 0;
    nvme_acq_head = 0;
    nvme_acq_phase = 1;
    nvme_cid_counter = 0;

    /* Set AQA (admin queue attributes): ASQS=ACQS=NVME_AQ_DEPTH-1 */
    uint32_t aqa = ((uint32_t)(NVME_AQ_DEPTH - 1) << 16) | (uint32_t)(NVME_AQ_DEPTH - 1);
    nvme_write32(0x24, aqa);

    /* Set ASQ and ACQ physical addresses */
    nvme_write64(0x28, (uint64_t)(uintptr_t)nvme_asq);
    nvme_write64(0x30, (uint64_t)(uintptr_t)nvme_acq);

    /* Configure CC: MPS=0 (4KB), CSS=0 (NVM), IOSQES=6 (64B), IOCQES=4 (16B), EN=1 */
    cc = (6u << 16)  /* IOSQES = 2^6 = 64 bytes */
       | (4u << 20)  /* IOCQES = 2^4 = 16 bytes */
       | 1u;         /* EN = 1 */
    nvme_write32(0x14, cc);

    /* Wait for CSTS.RDY = 1 */
    for (int spin = 0; spin < 4000000; spin++) {
        uint32_t csts = nvme_read32(0x1C);
        if (csts & 1u) {
            nvme_up = 1;
            serial_print("[nvme] controller ready\n");

            /* Identify Controller */
            int irc = nvme_identify_controller();
            if (irc == 0) {
                nvme_extract_str(&nvme_ident_buf[24], 20, nvme_serial, 21);
                nvme_extract_str(&nvme_ident_buf[24 + 20], 40, nvme_model, 41);
                nvme_ident_ok = 1;
            }
            /* Create I/O queue pair */
            if (nvme_setup_io_queues() == 0) {
                serial_print("[nvme] I/O queues ready\n");
            }
            return 0;
        }
        if (csts & (1u << 1)) return -2; /* CFS = fatal */
    }
    return -3; /* timeout */
}

int kernel_nvme_available(void) { return nvme_up; }
uint32_t kernel_nvme_version(void) { return nvme_vs; }

const char *kernel_nvme_model(void) {
    return nvme_ident_ok ? nvme_model : "";
}

const char *kernel_nvme_serial(void) {
    return nvme_ident_ok ? nvme_serial : "";
}

int kernel_nvme_identified(void) {
    return nvme_ident_ok;
}

/* =========================================================================
 * Phase 39: NVMe I/O Queue pair + Read/Write
 * ========================================================================= */

#define NVME_IOQ_DEPTH 16

static NvmeSqe  nvme_iosq[NVME_IOQ_DEPTH] __attribute__((aligned(4096)));
static NvmeCqe  nvme_iocq[NVME_IOQ_DEPTH] __attribute__((aligned(4096)));
static uint8_t  nvme_io_buf[4096]          __attribute__((aligned(4096)));

static uint16_t nvme_iosq_tail = 0;
static uint16_t nvme_iocq_head = 0;
static uint8_t  nvme_iocq_phase = 1;
static int      nvme_ioq_up = 0;

/* Create I/O Completion Queue (admin opcode 0x05) */
static int nvme_create_iocq(void) {
    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_asq_tail;

    nvme_memzero(&nvme_asq[slot], sizeof(NvmeSqe));
    nvme_asq[slot].cdw0 = 0x05u | ((uint32_t)cid << 16);
    nvme_asq[slot].prp1 = (uint64_t)(uintptr_t)nvme_iocq;
    nvme_asq[slot].cdw10 = ((uint32_t)(NVME_IOQ_DEPTH - 1) << 16) | 1u; /* size | CQID=1 */
    nvme_asq[slot].cdw11 = 1u; /* PC=1 (physically contiguous) */

    nvme_asq_tail++;
    if (nvme_asq_tail >= NVME_AQ_DEPTH) nvme_asq_tail = 0;
    nvme_ring_asq();

    return nvme_wait_acq(cid, 0);
}

/* Create I/O Submission Queue (admin opcode 0x01) */
static int nvme_create_iosq(void) {
    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_asq_tail;

    nvme_memzero(&nvme_asq[slot], sizeof(NvmeSqe));
    nvme_asq[slot].cdw0 = 0x01u | ((uint32_t)cid << 16);
    nvme_asq[slot].prp1 = (uint64_t)(uintptr_t)nvme_iosq;
    nvme_asq[slot].cdw10 = ((uint32_t)(NVME_IOQ_DEPTH - 1) << 16) | 1u; /* size | SQID=1 */
    nvme_asq[slot].cdw11 = (1u << 16) | 1u; /* CQID=1 | PC=1 */

    nvme_asq_tail++;
    if (nvme_asq_tail >= NVME_AQ_DEPTH) nvme_asq_tail = 0;
    nvme_ring_asq();

    return nvme_wait_acq(cid, 0);
}

static int nvme_setup_io_queues(void) {
    nvme_memzero(nvme_iosq, sizeof(nvme_iosq));
    nvme_memzero(nvme_iocq, sizeof(nvme_iocq));
    nvme_iosq_tail = 0;
    nvme_iocq_head = 0;
    nvme_iocq_phase = 1;

    int rc = nvme_create_iocq();
    if (rc != 0) return rc;
    rc = nvme_create_iosq();
    if (rc != 0) return rc;

    nvme_ioq_up = 1;
    return 0;
}

/* Ring I/O SQ1 tail doorbell: 0x1000 + (2*1)*stride */
static void nvme_ring_iosq(void) {
    nvme_write32(0x1000 + 2u * nvme_db_stride, nvme_iosq_tail);
}

/* Wait for I/O CQ1 completion */
static int nvme_wait_iocq(uint16_t cid) {
    for (int spin = 0; spin < 4000000; spin++) {
        volatile NvmeCqe *cqe = &nvme_iocq[nvme_iocq_head];
        uint8_t p = (uint8_t)(cqe->status & 1u);
        if (p == nvme_iocq_phase) {
            uint16_t got_cid = cqe->cid;
            uint16_t sc = (cqe->status >> 1) & 0x7FFu;

            nvme_iocq_head++;
            if (nvme_iocq_head >= NVME_IOQ_DEPTH) {
                nvme_iocq_head = 0;
                nvme_iocq_phase = (uint8_t)(nvme_iocq_phase ^ 1u);
            }
            /* CQ1 head doorbell: 0x1000 + (2*1+1)*stride */
            nvme_write32(0x1000 + 3u * nvme_db_stride, nvme_iocq_head);

            if (got_cid != cid) continue;
            return (sc == 0) ? 0 : -(int)sc;
        }
    }
    return -999;
}

/* NVMe Read (opcode 0x02, NVM command set) — up to 8 sectors (4KB) at a time */
int kernel_nvme_read(uint64_t lba, uint32_t sectors, uint8_t *out, uint32_t out_max) {
    if (!nvme_up || !nvme_ioq_up) return -1;
    if (sectors == 0 || sectors > 8) return -9;
    uint32_t bytes = sectors * 512u;
    if (!out || out_max < bytes) return -10;

    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_iosq_tail;

    nvme_memzero(&nvme_iosq[slot], sizeof(NvmeSqe));
    nvme_iosq[slot].cdw0 = 0x02u | ((uint32_t)cid << 16); /* Read */
    nvme_iosq[slot].nsid = 1;
    nvme_iosq[slot].prp1 = (uint64_t)(uintptr_t)nvme_io_buf;
    nvme_iosq[slot].prp2 = 0;
    nvme_iosq[slot].cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    nvme_iosq[slot].cdw11 = (uint32_t)(lba >> 32);
    nvme_iosq[slot].cdw12 = (sectors - 1u); /* NLB is 0-based */

    nvme_iosq_tail++;
    if (nvme_iosq_tail >= NVME_IOQ_DEPTH) nvme_iosq_tail = 0;
    nvme_ring_iosq();

    int rc = nvme_wait_iocq(cid);
    if (rc != 0) return rc;

    nvme_memcpy(out, nvme_io_buf, bytes);
    return (int)bytes;
}

/* NVMe Write (opcode 0x01, NVM command set) — up to 8 sectors (4KB) at a time */
int kernel_nvme_write(uint64_t lba, uint32_t sectors, const uint8_t *data, uint32_t data_len) {
    if (!nvme_up || !nvme_ioq_up) return -1;
    if (sectors == 0 || sectors > 8) return -9;
    uint32_t bytes = sectors * 512u;
    if (!data || data_len < bytes) return -10;

    nvme_memcpy(nvme_io_buf, data, bytes);

    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_iosq_tail;

    nvme_memzero(&nvme_iosq[slot], sizeof(NvmeSqe));
    nvme_iosq[slot].cdw0 = 0x01u | ((uint32_t)cid << 16); /* Write */
    nvme_iosq[slot].nsid = 1;
    nvme_iosq[slot].prp1 = (uint64_t)(uintptr_t)nvme_io_buf;
    nvme_iosq[slot].prp2 = 0;
    nvme_iosq[slot].cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    nvme_iosq[slot].cdw11 = (uint32_t)(lba >> 32);
    nvme_iosq[slot].cdw12 = (sectors - 1u);

    nvme_iosq_tail++;
    if (nvme_iosq_tail >= NVME_IOQ_DEPTH) nvme_iosq_tail = 0;
    nvme_ring_iosq();

    int rc = nvme_wait_iocq(cid);
    if (rc != 0) return rc;

    return (int)bytes;
}

// =============================================================================
// Phase 22: E1000 NIC Driver (Intel 82540EM)
// =============================================================================

#define E1000_VEN  0x8086
#define E1000_DEV  0x100E

#define E1000_CTRL   0x0000
#define E1000_STATUS 0x0008
#define E1000_ICR    0x00C0
#define E1000_IMS    0x00D0
#define E1000_IMC    0x00D8
#define E1000_RCTL   0x0100
#define E1000_TCTL   0x0400
#define E1000_TIPG   0x0410
#define E1000_RDBAL  0x2800
#define E1000_RDBAH  0x2804
#define E1000_RDLEN  0x2808
#define E1000_RDH    0x2810
#define E1000_RDT    0x2818
#define E1000_TDBAL  0x3800
#define E1000_TDBAH  0x3804
#define E1000_TDLEN  0x3808
#define E1000_TDH    0x3810
#define E1000_TDT    0x3818
#define E1000_RAL    0x5400
#define E1000_RAH    0x5404
#define E1000_MTA    0x5200

#define E1000_CTRL_RST  (1u << 26)
#define E1000_CTRL_SLU  (1u << 6)
#define E1000_CTRL_ASDE (1u << 5)

#define E1000_RCTL_EN    (1u << 1)
#define E1000_RCTL_BAM   (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)

#define E1000_TCTL_EN   (1u << 1)
#define E1000_TCTL_PSP  (1u << 3)

#define E1000_TXD_CMD_EOP  0x01
#define E1000_TXD_CMD_IFCS 0x02
#define E1000_TXD_CMD_RS   0x08

#define E1000_NUM_RX 32
#define E1000_NUM_TX 8
#define E1000_PKT_SIZE 2048

typedef struct { uint64_t addr; uint16_t length; uint16_t checksum;
                 uint8_t status; uint8_t errors; uint16_t special;
} __attribute__((packed)) E1000RxDesc;

typedef struct { uint64_t addr; uint16_t length; uint8_t cso; uint8_t cmd;
                 uint8_t status; uint8_t css; uint16_t special;
} __attribute__((packed)) E1000TxDesc;

static volatile uint8_t *e1000_mmio = 0;
static uint8_t e1000_mac[6];
static int e1000_up = 0;

static E1000RxDesc e1000_rx_ring[E1000_NUM_RX] __attribute__((aligned(16)));
static E1000TxDesc e1000_tx_ring[E1000_NUM_TX] __attribute__((aligned(16)));
static uint8_t e1000_rx_bufs[E1000_NUM_RX][E1000_PKT_SIZE] __attribute__((aligned(16)));
static uint8_t e1000_tx_bufs[E1000_NUM_TX][E1000_PKT_SIZE] __attribute__((aligned(16)));
static uint32_t e1000_rx_cur, e1000_tx_cur;

static void e1000_w(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(e1000_mmio + reg) = val;
}
static uint32_t e1000_r(uint32_t reg) {
    return *(volatile uint32_t *)(e1000_mmio + reg);
}

static int e1000_init(PciDev *dev) {
    // Enable bus mastering + memory space
    uint32_t cmd = pci_read32(dev->bus, dev->slot, 0, 0x04);
    cmd |= (1u << 2) | (1u << 1);
    pci_write32(dev->bus, dev->slot, 0, 0x04, cmd);

    // BAR0 MMIO address
    uint64_t mmio_phys = dev->bar0 & ~0xFULL;
    // If 64-bit BAR, read upper 32 bits
    if ((dev->bar0 & 0x6) == 0x4) {
        uint32_t bar1 = pci_read32(dev->bus, dev->slot, 0, 0x14);
        mmio_phys |= ((uint64_t)bar1 << 32);
    }

    // Map 128KB MMIO region
    for (uint64_t off = 0; off < 0x20000; off += PAGE_SIZE) {
        paging_map_4k(kernel_pml4, mmio_phys + off, mmio_phys + off,
                      PAGE_PRESENT | PAGE_WRITABLE);
    }
    paging_flush_tlb();
    e1000_mmio = (volatile uint8_t *)mmio_phys;

    // Reset
    e1000_w(E1000_CTRL, e1000_r(E1000_CTRL) | E1000_CTRL_RST);
    for (volatile int i = 0; i < 1000000; i++) {}

    // Disable interrupts
    e1000_w(E1000_IMC, 0xFFFFFFFF);
    (void)e1000_r(E1000_ICR);

    // Link up
    uint32_t ctrl = e1000_r(E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~(E1000_CTRL_RST | (1u << 3) | (1u << 31) | (1u << 30)); // Clear RST, LRST, PHY_RST, VME
    e1000_w(E1000_CTRL, ctrl);

    // Wait for link up
    for (volatile int i = 0; i < 1000000; i++) {}

    // Read MAC from RAL/RAH
    uint32_t ral = e1000_r(E1000_RAL);
    uint32_t rah = e1000_r(E1000_RAH);
    e1000_mac[0] = ral; e1000_mac[1] = ral >> 8;
    e1000_mac[2] = ral >> 16; e1000_mac[3] = ral >> 24;
    e1000_mac[4] = rah; e1000_mac[5] = rah >> 8;
    // Ensure Address Valid bit is set
    e1000_w(E1000_RAH, (rah & 0xFFFF) | (1u << 31));

    // Clear multicast table
    for (int i = 0; i < 128; i++) e1000_w(E1000_MTA + i * 4, 0);

    // Init RX ring
    for (int i = 0; i < E1000_NUM_RX; i++) {
        e1000_rx_ring[i].addr = (uint64_t)(uintptr_t)e1000_rx_bufs[i];
        e1000_rx_ring[i].status = 0;
    }
    e1000_w(E1000_RDBAL, (uint32_t)((uintptr_t)e1000_rx_ring));
    e1000_w(E1000_RDBAH, (uint32_t)((uintptr_t)e1000_rx_ring >> 32));
    e1000_w(E1000_RDLEN, E1000_NUM_RX * sizeof(E1000RxDesc));
    e1000_w(E1000_RDH, 0);
    e1000_w(E1000_RDT, E1000_NUM_RX - 1);
    e1000_w(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    e1000_rx_cur = 0;

    // Init TX ring
    for (int i = 0; i < E1000_NUM_TX; i++) {
        e1000_tx_ring[i].addr = (uint64_t)(uintptr_t)e1000_tx_bufs[i];
        e1000_tx_ring[i].status = 1;  // DD set = ready
        e1000_tx_ring[i].cmd = 0;
    }
    e1000_w(E1000_TDBAL, (uint32_t)((uintptr_t)e1000_tx_ring));
    e1000_w(E1000_TDBAH, (uint32_t)((uintptr_t)e1000_tx_ring >> 32));
    e1000_w(E1000_TDLEN, E1000_NUM_TX * sizeof(E1000TxDesc));
    e1000_w(E1000_TDH, 0);
    e1000_w(E1000_TDT, 0);
    e1000_w(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (15u << 4) | (64u << 12));
    e1000_w(E1000_TIPG, 10 | (10 << 10) | (10 << 20));
    e1000_tx_cur = 0;

    e1000_up = 1;
    return 0;
}

static int e1000_send(const void *data, uint16_t len) {
    if (!e1000_up || len > E1000_PKT_SIZE) return -1;
    uint32_t idx = e1000_tx_cur;
    while (!(e1000_tx_ring[idx].status & 1)) { __asm__ volatile("pause"); }
    const uint8_t *s = (const uint8_t *)data;
    for (uint16_t i = 0; i < len; i++) e1000_tx_bufs[idx][i] = s[i];
    e1000_tx_ring[idx].length = len;
    e1000_tx_ring[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    e1000_tx_ring[idx].status = 0;
    e1000_tx_cur = (idx + 1) % E1000_NUM_TX;
    e1000_w(E1000_TDT, e1000_tx_cur);
    return 0;
}

static int e1000_recv(void *buf, uint16_t max_len) {
    if (!e1000_up) return -1;
    uint32_t idx = e1000_rx_cur;
    if (!(e1000_rx_ring[idx].status & 1)) return 0;
    uint16_t len = e1000_rx_ring[idx].length;
    if (len > max_len) len = max_len;
    uint8_t *d = (uint8_t *)buf; const uint8_t *s = e1000_rx_bufs[idx];
    for (uint16_t i = 0; i < len; i++) d[i] = s[i];
    e1000_rx_ring[idx].status = 0;
    uint32_t old = idx;
    e1000_rx_cur = (idx + 1) % E1000_NUM_RX;
    e1000_w(E1000_RDT, old);
    return len;
}

// =============================================================================
// Phase 22: Minimal Network Stack (ARP + ICMP)
// =============================================================================

static uint8_t net_ip[4]  = {10, 0, 2, 15};
static uint8_t net_gw[4]  = {10, 0, 2, 2};
static uint8_t net_bcast_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

#define NET_ARP_CACHE 8
typedef struct { uint8_t ip[4]; uint8_t mac[6]; uint8_t valid; } ArpEntry;
static ArpEntry arp_cache[NET_ARP_CACHE];

static int ip_eq(const uint8_t *a, const uint8_t *b) {
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}
static void mcpy(void *d, const void *s, int n) {
    for (int i = 0; i < n; i++) ((uint8_t*)d)[i] = ((const uint8_t*)s)[i];
}
static void mzero(void *d, int n) {
    for (int i = 0; i < n; i++) ((uint8_t*)d)[i] = 0;
}
static uint16_t htons(uint16_t h) { return (h >> 8) | (h << 8); }
static uint16_t ntohs(uint16_t n) { return htons(n); }

static uint16_t ip_checksum(const void *data, int len) {
    // Internet checksum over network-order 16-bit words.
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len) sum += ((uint16_t)p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return htons((uint16_t)(~sum));
}

typedef struct { uint8_t dst[6]; uint8_t src[6]; uint16_t ethertype;
} __attribute__((packed)) EthHdr;

typedef struct { uint16_t htype; uint16_t ptype; uint8_t hlen; uint8_t plen;
    uint16_t oper; uint8_t sha[6]; uint8_t spa[4]; uint8_t tha[6]; uint8_t tpa[4];
} __attribute__((packed)) ArpPkt;

typedef struct { uint8_t ihl_ver; uint8_t tos; uint16_t total_len;
    uint16_t ident; uint16_t frag_off; uint8_t ttl; uint8_t protocol;
    uint16_t checksum; uint8_t src[4]; uint8_t dst[4];
} __attribute__((packed)) Ipv4Hdr;

typedef struct { uint8_t type; uint8_t code; uint16_t checksum;
    uint16_t ident; uint16_t seq;
} __attribute__((packed)) IcmpHdr;

static void net_send_arp_request(const uint8_t *target_ip) {
    uint8_t pkt[14 + 28];
    EthHdr *eth = (EthHdr *)pkt; ArpPkt *arp = (ArpPkt *)(pkt + 14);
    mcpy(eth->dst, net_bcast_mac, 6); mcpy(eth->src, e1000_mac, 6);
    eth->ethertype = htons(0x0806);
    arp->htype = htons(1); arp->ptype = htons(0x0800);
    arp->hlen = 6; arp->plen = 4; arp->oper = htons(1);
    mcpy(arp->sha, e1000_mac, 6); mcpy(arp->spa, net_ip, 4);
    mzero(arp->tha, 6); mcpy(arp->tpa, target_ip, 4);
    e1000_send(pkt, sizeof(pkt));
}

static void net_handle_arp(const uint8_t *data, int len) {
    if (len < 28) return;
    const ArpPkt *arp = (const ArpPkt *)data;
    // ARP request for us → reply
    if (ntohs(arp->oper) == 1 && ip_eq(arp->tpa, net_ip)) {
        uint8_t pkt[14 + 28];
        EthHdr *eth = (EthHdr *)pkt; ArpPkt *r = (ArpPkt *)(pkt + 14);
        mcpy(eth->dst, arp->sha, 6); mcpy(eth->src, e1000_mac, 6);
        eth->ethertype = htons(0x0806);
        r->htype = htons(1); r->ptype = htons(0x0800);
        r->hlen = 6; r->plen = 4; r->oper = htons(2);
        mcpy(r->sha, e1000_mac, 6); mcpy(r->spa, net_ip, 4);
        mcpy(r->tha, arp->sha, 6); mcpy(r->tpa, arp->spa, 4);
        e1000_send(pkt, sizeof(pkt));
    }
    // ARP reply → cache
    if (ntohs(arp->oper) == 2) {
        for (int i = 0; i < NET_ARP_CACHE; i++) {
            if (!arp_cache[i].valid || ip_eq(arp_cache[i].ip, arp->spa)) {
                mcpy(arp_cache[i].ip, arp->spa, 4);
                mcpy(arp_cache[i].mac, arp->sha, 6);
                arp_cache[i].valid = 1;
                return;
            }
        }
    }
}

static int net_arp_resolve(const uint8_t *ip, uint8_t *mac_out) {
    for (int i = 0; i < NET_ARP_CACHE; i++)
        if (arp_cache[i].valid && ip_eq(arp_cache[i].ip, ip))
            { mcpy(mac_out, arp_cache[i].mac, 6); return 0; }
    net_send_arp_request(ip);
    uint8_t buf[E1000_PKT_SIZE];
    uint64_t start = kernel_tick;
    uint64_t last_arp = start;
    while ((kernel_tick - start) < (TIMER_HZ * 5)) {
        int n = e1000_recv(buf, sizeof(buf));
        if (n >= 14) {
            EthHdr *eth = (EthHdr *)buf;
            if (ntohs(eth->ethertype) == 0x0806) net_handle_arp(buf + 14, n - 14);
        }
        for (int i = 0; i < NET_ARP_CACHE; i++)
            if (arp_cache[i].valid && ip_eq(arp_cache[i].ip, ip))
                { mcpy(mac_out, arp_cache[i].mac, 6); return 0; }
        if ((kernel_tick - last_arp) >= TIMER_HZ) {
            net_send_arp_request(ip);
            last_arp = kernel_tick;
        }
        __asm__ volatile("hlt");
    }
    return -1;
}

static int net_ping_one(const uint8_t *dst_ip, const uint8_t *dst_mac, uint16_t seq) {
    uint8_t pkt[14 + 20 + 8 + 32]; // Eth + IP + ICMP + 32 payload = 74
    EthHdr *eth = (EthHdr *)pkt;
    Ipv4Hdr *ip = (Ipv4Hdr *)(pkt + 14);
    IcmpHdr *icmp = (IcmpHdr *)(pkt + 14 + 20);
    uint8_t *payload = pkt + 14 + 20 + 8;

    mcpy(eth->dst, dst_mac, 6); mcpy(eth->src, e1000_mac, 6);
    eth->ethertype = htons(0x0800);

    ip->ihl_ver = 0x45; ip->tos = 0; ip->total_len = htons(20 + 8 + 32);
    ip->ident = htons(seq); ip->frag_off = 0; ip->ttl = 64; ip->protocol = 1;
    ip->checksum = 0; mcpy(ip->src, net_ip, 4); mcpy(ip->dst, dst_ip, 4);
    ip->checksum = ip_checksum(ip, 20);

    icmp->type = 8; icmp->code = 0; icmp->checksum = 0;
    icmp->ident = htons(0x1234); icmp->seq = htons(seq);
    for (int i = 0; i < 32; i++) payload[i] = (uint8_t)i;
    icmp->checksum = ip_checksum(icmp, 8 + 32);

    e1000_send(pkt, sizeof(pkt));

    // Wait for echo reply (3s timeout)
    uint8_t buf[E1000_PKT_SIZE];
    uint64_t start = kernel_tick;
    while ((kernel_tick - start) < (TIMER_HZ * 3)) {
        int n = e1000_recv(buf, sizeof(buf));
        if (n >= 14) {
            EthHdr *re = (EthHdr *)buf;
            uint16_t etype = ntohs(re->ethertype);
            if (etype == 0x0806) { net_handle_arp(buf + 14, n - 14); continue; }
            if (etype == 0x0800 && n >= 14 + 20 + 8) {
                Ipv4Hdr *ri = (Ipv4Hdr *)(buf + 14);
                if (ri->protocol == 1) {
                    IcmpHdr *rc = (IcmpHdr *)(buf + 14 + 20);
                    if (rc->type == 0 && ntohs(rc->seq) == seq) return 1;
                }
            }
        }
        __asm__ volatile("hlt");
    }
    return 0;
}

// Exported for CLI
int kernel_net_available(void) { return e1000_up; }
void kernel_net_get_ip(uint8_t *out) { mcpy(out, net_ip, 4); }
void kernel_net_get_mac(uint8_t *out) { mcpy(out, e1000_mac, 6); }

int kernel_net_ping(uint8_t a, uint8_t b, uint8_t c, uint8_t d, int count) {
    if (!e1000_up) return -1;
    uint8_t dst_ip[4] = {a, b, c, d};
    uint8_t dst_mac[6];
    if (net_arp_resolve(dst_ip, dst_mac) < 0) return -2;
    int ok = 0;
    for (int i = 0; i < count; i++) {
        if (net_ping_one(dst_ip, dst_mac, (uint16_t)(i + 1))) ok++;
    }
    return ok;
}

// =============================================================================
// Kernel entry point
// =============================================================================

void kernel_main(void) __attribute__((used));

void kernel_main(void) {
    // Stack is already set to 0xF00000 by _start in interrupts.asm
    // (above BSS end ~9.3 MB, within mapped 0-16 MB)

    // Enable SSE/SSE2.  The Rust library's memset/memcpy use SSE2 instructions
    // (movups, pshufd, etc.).  Without this, any Rust call that touches memset
    // causes a #UD -> triple fault before the IDT is even loaded.
    // CR0: clear EM (bit 2) and TS (bit 3), set MP (bit 1)
    // CR4: set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    {
        uint64_t cr0, cr4;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 = (cr0 & ~((1UL<<2)|(1UL<<3))) | (1UL<<1);
        __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1UL<<9) | (1UL<<10);
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
    }

    // Zero BSS.  objcopy -O binary omits SHT_NOBITS sections, so every static
    // that Rust/C expects to be zero-initialised (ALLOCATOR spin-lock, ring
    // buffers, GDT/IDT arrays, …) contains whatever garbage RAM held at boot.
    // In particular LockedHeap::empty() relies on the mutex word being 0 —
    // without this the very first ALLOCATOR.lock() spins forever.
    {
        extern char _bss_start[], _bss_end[];
        volatile char *p = _bss_start;
        while (p < (volatile char *)_bss_end) *p++ = 0;
    }

    // ----- Serial output (Phase 1) -----
    serial_init();
    serial_print("\n[VernisOS] kernel_main() entered\n");

    // ----- Detect framebuffer from bootloader -----
    {
        volatile struct boot_info *bi = (volatile struct boot_info *)0x5300;
        if (bi->magic == 0x56424549 && bi->fb_type == 1) {
            display_mode = 1;
            serial_print("[fb] Framebuffer detected\n");
        } else {
            serial_print("[fb] No framebuffer, using VGA text mode\n");
        }
    }

    serial_print("[dbg] VGA init\n");
    // ----- VGA -----
    terminal_initialize();
    serial_print("[dbg] clear_screen\n");
    clear_screen();

    serial_print("[dbg] heap init addr=");
    serial_print_hex((uint64_t)kernel_heap);
    serial_print("\n");
    // ----- Heap init (must be before any Rust allocation) -----
    verniskernel_init_heap((uint64_t)kernel_heap, HEAP_SIZE);
    serial_print("[heap] initialized, size=8MB\n");

    // ----- Phase 16: Paging — rebuild page tables in kernel memory -----
    paging_init();

    // ----- Framebuffer init (after heap, before Phase 3) -----
    if (display_mode == 1) {
        volatile struct boot_info *bi = (volatile struct boot_info *)0x5300;
        uint64_t fb_addr = (uint64_t)bi->fb_addr | ((uint64_t)bi->fb_addr_high << 32);
        fb_init(fb_addr, bi->fb_width, bi->fb_height, bi->fb_pitch, bi->fb_bpp);
        console_init(bi->fb_width, bi->fb_height);
        console_clear();
        serial_print("[fb] Framebuffer mode: ");
        serial_print_dec(bi->fb_width);
        serial_print("x");
        serial_print_dec(bi->fb_height);
        serial_print("x");
        serial_print_dec(bi->fb_bpp);
        serial_print(" @ ");
        serial_print_hex((uint32_t)fb_addr);
        serial_print("\n");
    }

    serial_print("[dbg] VGA text\n");
    terminal_setcolor(make_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("VernisOS x86-64 Kernel\n");
    terminal_writestring("======================\n");

    // ----- GDT + TSS (Phase 2) -----
    gdt_init();
    serial_print("[gdt] GDT + TSS loaded\n");

    // ----- IDT (Phase 2) -----
    idt_init();
    serial_print("[idt] IDT loaded (256 gates)\n");

    // ----- PIC remap (Phase 2) -----
    pic_init();
    serial_print("[pic] 8259A remapped: IRQ0-7 -> 0x20, IRQ8-15 -> 0x28\n");

    // ----- PIT timer 240Hz (Phase 2) -----
    pit_init();
    serial_print("[pit] PIT timer set to 240Hz\n");

    // ----- Keyboard init -----
    keyboard_init();
    serial_print("[kbd] keyboard buffer initialized\n");

    // ----- Phase 42: TTY init -----
    tty_init(&kernel_tty0);
    serial_print("[tty] tty0 initialized\n");

    // ----- Mouse init (GUI mode) -----
    if (display_mode >= 1) {
        ps2_mouse_init();
        volatile struct boot_info *bi = (volatile struct boot_info *)0x5300;
        mouse_init(bi->fb_width, bi->fb_height);
    }

    // ----- Phase 3: Rust print callback + syscall + scheduler -----
    // Must run after GDT+IDT so any Rust exception can be caught
    serial_print("[phase3] register_print...\n");
    verniskernel_register_print(rust_print_cb);
    serial_print("[phase3] syscall_init...\n");
    syscall_init();
    serial_print("[phase3] scheduler_new...\n");
    kernel_scheduler = scheduler_new();
    serial_print("[phase3] create_process...\n");
    uint32_t init_pid = scheduler_create_process(kernel_scheduler, 10, "init");
    uint32_t ai_pid   = scheduler_create_process(kernel_scheduler, 8,  "ai_engine");
    (void)init_pid;
    (void)ai_pid;
    // Phase 10: Notify AI of initial process creation
    {
        char evbuf[48];
        char pidbuf[12]; int i = 0; char tmp[12]; uint32_t v = init_pid;
        if (v == 0) { tmp[i++] = '0'; } else { while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; } }
        int p = 0; for (int j = i - 1; j >= 0; j--) pidbuf[p++] = tmp[j]; pidbuf[p] = '\0';
        ai_build_event(evbuf, sizeof(evbuf), pidbuf, "fork", "init");
        ai_send_event(AI_EVT_PROCESS, evbuf);
        ai_kernel_engine_feed(AI_EVT_PROCESS, evbuf, kernel_tick);
    }
    serial_print("[phase3] schedule...\n");
    scheduler_schedule(kernel_scheduler);
    serial_print("[phase3] done\n");

    // ----- Phase 4: IPC -----
    serial_print("[phase4] ipc_init...\n");
    ipc_init();
    serial_print("[phase4] done\n");

    // ----- Phase 5: Module Loader -----
    serial_print("[phase5] module_init...\n");
    module_init();
    serial_print("[phase5] done\n");

    // ----- Phase 6: User Sandbox Environment -----
    serial_print("[phase6] sandbox_init...\n");
    sandbox_init();
    serial_print("[phase6] done\n");

    // ----- Phase 8: AI IPC Bridge -----
    serial_print("[phase8] ai_bridge_init...\n");
    ai_bridge_init();
    ai_send_event(AI_EVT_BOOT, "vernisOS x86_64 kernel started");
    ai_set_tune_handler(kernel_tune_handler);
    ai_set_remediate_handler(kernel_remediate_handler);
    // Phase 10: In-kernel Rust AI engine
    ai_kernel_engine_init(kernel_scheduler);
    // Phase 26: ACPI-lite power management init
    acpi_init();
    // Phase 12: Load policy from disk
    policy_load_from_disk();
    // Phase 13: VernisFS + User DB
    kfs_init();
    // Phase 48: Block cache
    bcache_init();
    userdb_init();
    // Phase 14: Audit log
    auditlog_init();
    // Phase 16: Structured logging
    klog_init();
    KLOG_INFO("boot", "kernel subsystems initialized");
    ai_kernel_engine_feed(AI_EVT_BOOT, "vernisOS x86_64 kernel started", kernel_tick);
    serial_print("[phase8] done\n");


    // ----- Phase 7: CLI / Terminal System -----
    serial_print("[phase7] cli_init...\n");
    CliShell *shell = cli_shell_init();
    CliSession *user_session = cli_session_create(shell, "root", CLI_PRIV_ROOT);
    gui_shell = shell;
    gui_session = user_session;
    serial_print("[phase7] done\n");

    // ----- SYSCALL/SYSRET (Phase 5) -----
    syscall_hw_init();

    // ----- Phase 18: Preemptive Context Switch -----
    serial_print("[phase18] context switch setup...\n");
    task_register_main(init_pid, 24);   // 100ms quantum at 240 Hz
    {
        uint32_t worker_pid = scheduler_create_process(kernel_scheduler, 9, "worker");
        int worker_idx = task_create(phase18_worker_entry, worker_pid, 24);
        if (worker_idx >= 0) {
            serial_print("[phase18] worker task created (pid=");
            serial_print_dec(worker_pid);
            serial_print(")\n");
        }
    }
    context_switch_enabled = 1;
    serial_print("[phase18] preemptive multitasking enabled\n");

    // ----- Phase 45: User shell boot -----
    g_elf_exec_fn = kernel_elf_exec;
    {
        const void *vsh = vfs_find_file("/bin/vsh64");
        if (vsh) {
            serial_print("[phase45] found /bin/vsh64, launching user shell...\n");
            int elf_slot = elf_exec("/bin/vsh64", 24);
            if (elf_slot >= 0) {
                serial_print("[phase45] user shell task running\n");
            } else {
                serial_print("[phase45] /bin/vsh64 load failed\n");
            }
        } else {
            serial_print("[phase45] /bin/vsh64 not found — shell-only boot mode\n");
        }
    }

    // ----- Phase 21: RTC test -----
    {
        RtcTime t;
        rtc_read(&t);
        serial_print("[phase21] RTC: ");
        serial_print_dec(t.year);
        serial_print("-");
        serial_print_dec(t.month);
        serial_print("-");
        serial_print_dec(t.day);
        serial_print(" ");
        serial_print_dec(t.hour);
        serial_print(":");
        serial_print_dec(t.minute);
        serial_print(":");
        serial_print_dec(t.second);
        serial_print(" UTC\n");
    }

    // ----- Phase 22: PCI + Network -----
    serial_print("[phase22] PCI scan...\n");
    pci_scan();
    serial_print("[phase22] PCI: ");
    serial_print_dec(pci_dev_count);
    serial_print(" devices found\n");
    for (int i = 0; i < pci_dev_count; i++) {
        serial_print("  ");
        serial_print_hex(pci_devs[i].vendor_id);
        serial_print(":");
        serial_print_hex(pci_devs[i].device_id);
        serial_print(" class=");
        serial_print_hex(pci_devs[i].class_code);
        serial_print(":");
        serial_print_hex(pci_devs[i].subclass);
        serial_print("\n");
    }
    {
        int found = 0;
        for (int i = 0; i < pci_dev_count; i++) {
            if (pci_devs[i].class_code == 0x01 && pci_devs[i].subclass == 0x06) {
                serial_print("[phase32] AHCI candidate at ");
                serial_print_dec(pci_devs[i].bus);
                serial_print(":");
                serial_print_dec(pci_devs[i].slot);
                serial_print(" ABAR=");
                serial_print_hex(pci_read32(pci_devs[i].bus, pci_devs[i].slot, 0, 0x24));
                serial_print("\n");
                if (ahci_init(&pci_devs[i]) == 0) {
                    serial_print("[phase32] AHCI online: ports=");
                    serial_print_dec((uint32_t)ahci_ports);
                    serial_print(" PI=");
                    serial_print_hex(ahci_pi);
                    serial_print(" VS=");
                    serial_print_hex(ahci_vs);
                    serial_print("\n");
                    uint32_t max_ports = (ahci_cap & 0x1Fu) + 1u;
                    if (max_ports > 32u) max_ports = 32u;
                    for (uint32_t p = 0; p < max_ports; p++) {
                        if (!(ahci_pi & (1u << p))) continue;
                        uint32_t ssts = 0, sig = 0;
                        (void)kernel_ahci_port_info((int)p, &ssts, &sig, 0, 0, 0);
                        serial_print("[phase32] AHCI p");
                        serial_print_dec(p);
                        serial_print(" ");
                        int ptype = ahci_port_sig_type(sig, ssts);
                        if (ptype == 0) serial_print("no-link");
                        else if (ptype == 1) serial_print("SATA");
                        else if (ptype == 2) serial_print("SATAPI");
                        else if (ptype == 3) serial_print("SEMB");
                        else if (ptype == 4) serial_print("PM");
                        else serial_print("unknown");
                        serial_print(" sig=");
                        serial_print_hex(sig);
                        serial_print(" ssts=");
                        serial_print_hex(ssts);
                        if (ptype == 1) {
                            int idr = kernel_ahci_identify((int)p);
                            serial_print(" identify=");
                            serial_print_dec((uint32_t)idr);
                            if (idr == 0) {
                                serial_print(" model=");
                                serial_print(kernel_ahci_model((int)p));
                            }
                        }
                        serial_print("\n");
                    }
                    found = 1;
                }
                break;
            }
        }
        if (!found) serial_print("[phase32] no AHCI controller found\n");
    }
    /* NVMe probe (PCI class 01:08) */
    {
        int found = 0;
        for (int i = 0; i < pci_dev_count; i++) {
            if (pci_devs[i].class_code == 0x01 && pci_devs[i].subclass == 0x08) {
                serial_print("[phase38] NVMe candidate at ");
                serial_print_dec(pci_devs[i].bus);
                serial_print(":");
                serial_print_dec(pci_devs[i].slot);
                serial_print(" BAR0=");
                serial_print_hex(pci_read32(pci_devs[i].bus, pci_devs[i].slot, 0, 0x10));
                serial_print("\n");
                if (nvme_init(&pci_devs[i]) == 0) {
                    serial_print("[phase38] NVMe VS=");
                    serial_print_hex(nvme_vs);
                    serial_print(" MQES=");
                    serial_print_dec(nvme_mqes);
                    serial_print("\n");
                    if (nvme_ident_ok) {
                        serial_print("[phase38] NVMe model=");
                        serial_print(nvme_model);
                        serial_print(" serial=");
                        serial_print(nvme_serial);
                        serial_print("\n");
                    }
                    found = 1;
                }
                break;
            }
        }
        if (!found) serial_print("[phase38] no NVMe controller found\n");
    }
    // Find and init E1000 NIC
    {
        int found = 0;
        for (int i = 0; i < pci_dev_count; i++) {
            if (pci_devs[i].vendor_id == E1000_VEN &&
                pci_devs[i].device_id == E1000_DEV) {
                serial_print("[phase22] E1000 found at ");
                serial_print_dec(pci_devs[i].bus);
                serial_print(":");
                serial_print_dec(pci_devs[i].slot);
                serial_print(" BAR0=");
                serial_print_hex(pci_devs[i].bar0);
                serial_print("\n");
                if (e1000_init(&pci_devs[i]) == 0) {
                    serial_print("[phase22] E1000 MAC=");
                    for (int j = 0; j < 6; j++) {
                        if (j) serial_print(":");
                        serial_print_hex(e1000_mac[j]);
                    }
                    serial_print("\n");
                    serial_print("[phase22] Network: 10.0.2.15 (static)\n");
                    found = 1;
                }
                break;
            }
        }
        if (!found) serial_print("[phase22] no E1000 NIC found\n");
    }

    // ----- Enable interrupts -----
    __asm__ volatile("sti");
    serial_print("[kernel] interrupts enabled\n");

    // Phase 22: boot-time ping test (gateway 10.0.2.2)
    if (e1000_up) {
        serial_print("[phase22] ping 10.0.2.2...\n");
        int pr = kernel_net_ping(10, 0, 2, 2, 1);
        if (pr > 0) serial_print("[phase22] ping OK\n");
        else if (pr == -2) serial_print("[phase22] ARP failed\n");
        else serial_print("[phase22] ICMP timeout\n");
    }

    // Clear boot messages before starting interactive shell
    clear_screen();
    terminal_row = terminal_column = 0;
    terminal_setcolor(make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    // ----- GUI mode -----
    if (display_mode == 1) {
        volatile struct boot_info *bi = (volatile struct boot_info *)0x5300;
        serial_print("[gui] calling gui_init(");
        serial_print_dec(bi->fb_width);
        serial_print(",");
        serial_print_dec(bi->fb_height);
        serial_print(")\n");
        gui_init(bi->fb_width, bi->fb_height);
        display_mode = 2;
        serial_print("[gui] GUI initialized, entering main loop\n");

        while (1) {
            gui_main_loop_tick();
            __asm__ volatile("hlt");
        }
    }

    // Run interactive shell (VGA text mode)
    if (user_session) {
        cli_shell_loop(user_session);
    }

    // If shell exits, just halt
    serial_print("[kernel] shell exited\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
