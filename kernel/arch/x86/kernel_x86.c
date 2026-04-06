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
// I/O Port helpers
// =============================================================================

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
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
static inline void io_wait(void) { outb(0x80, 0); }

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
extern void fb_init(uint32_t addr, uint32_t width, uint32_t height,
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
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_GREY    7
#define VGA_COLOR_LIGHT_BROWN   14
#define VGA_COLOR_WHITE         15
#define VGA_COLOR_YELLOW        VGA_COLOR_LIGHT_BROWN

static const size_t VGA_WIDTH  = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t * const VGA_MEMORY = (uint16_t *)0xB8000;

static size_t   terminal_row;
static size_t   terminal_column;
static uint8_t  terminal_color;
static uint16_t *terminal_buffer;

static inline uint8_t  make_color(uint8_t fg, uint8_t bg) { return fg | (bg << 4); }
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

static void terminal_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            terminal_buffer[y * VGA_WIDTH + x] = terminal_buffer[(y+1) * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        terminal_buffer[(VGA_HEIGHT-1) * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
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
    terminal_buffer[terminal_row * VGA_WIDTH + terminal_column] = make_vgaentry(c, terminal_color);
    if (++terminal_column >= VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row >= VGA_HEIGHT) terminal_scroll();
    }
}

static void terminal_writestring(const char *s) {
    for (; *s; s++) terminal_putchar(*s);
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
    if (display_mode >= 2) return;
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
    uint8_t col = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = make_vgaentry(' ', col);
    terminal_row    = 0;
    terminal_column = 0;
    terminal_color  = col;
}

void vga_set_cursor(size_t row, size_t col) {
    if (display_mode >= 1) return;
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

void vga_enable_cursor(void) {
    if (display_mode >= 1) return;
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
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = {0, 0};
    __asm__ volatile("lidt %0" :: "m"(null_idt));
    __asm__ volatile("int $0");  // no handler → triple fault → CPU reset
    while (1) __asm__ volatile("hlt");
}

static void clear_screen(void) {
    uint8_t col = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = make_vgaentry(' ', col);
    terminal_row = terminal_column = 0;
}

static void terminal_write_at(const char *s, size_t row, uint8_t color) {
    for (size_t x = 0; *s && x < VGA_WIDTH; s++, x++)
        VGA_MEMORY[row * VGA_WIDTH + x] = make_vgaentry(*s, color);
}

// =============================================================================
// Serial COM1 (Phase 1 — debug output)
// =============================================================================

#define COM1 0x3F8

static void serial_init(void) {
    outb(COM1 + 1, 0x00);  // disable interrupts
    outb(COM1 + 3, 0x80);  // DLAB on
    outb(COM1 + 0, 0x01);  // divisor lo: 115200 baud
    outb(COM1 + 1, 0x00);  // divisor hi
    outb(COM1 + 3, 0x03);  // 8N1
    outb(COM1 + 2, 0xC7);  // FIFO, clear, 14-byte threshold
    outb(COM1 + 4, 0x0B);  // RTS/DSR on
    outb(COM1 + 1, 0x00);  // disable UART interrupts (polling)
}

static void serial_putchar(char c) {
    while (!(inb(COM1 + 5) & 0x20));
    outb(COM1, (uint8_t)c);
}

void serial_print(const char *s) {
    for (; *s; s++) serial_putchar(*s);
}

void serial_print_hex(uint32_t val) {
    serial_print("0x");
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (val >> (i * 4)) & 0xF;
        serial_putchar(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
}

void serial_print_dec(uint32_t val) {
    if (val == 0) { serial_putchar('0'); return; }
    char buf[12]; int pos = 0;
    while (val) { buf[pos++] = '0' + val % 10; val /= 10; }
    for (int i = pos - 1; i >= 0; i--) serial_putchar(buf[i]);
}

// =============================================================================
// GDT — 6 entries (32-bit flat) + TSS
// =============================================================================

// x86 TSS structure (software portion only; I/O bitmap not used)
typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;      // Kernel stack pointer for Ring 0
    uint32_t ss0;       // Kernel stack segment for Ring 0
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) Tss32;

static Tss32 kernel_tss32 __attribute__((aligned(16)));

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) GdtEntry32;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) GdtPointer32;

static GdtEntry32 gdt32[6];
static GdtPointer32 gdt32_ptr;

static void gdt32_set(int idx, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t gran) {
    gdt32[idx].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt32[idx].base_low    = (uint16_t)(base  & 0xFFFF);
    gdt32[idx].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt32[idx].access      = access;
    gdt32[idx].granularity = (uint8_t)(gran | ((limit >> 16) & 0x0F));
    gdt32[idx].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

static void gdt_init(void) {
    gdt32_set(0, 0, 0,          0x00, 0x00);   // null
    gdt32_set(1, 0, 0xFFFFF,   0x9A, 0xC0);   // 0x08: kernel code 32-bit
    gdt32_set(2, 0, 0xFFFFF,   0x92, 0xC0);   // 0x10: kernel data 32-bit
    gdt32_set(3, 0, 0xFFFFF,   0xFA, 0xC0);   // 0x18: user   code 32-bit DPL=3
    gdt32_set(4, 0, 0xFFFFF,   0xF2, 0xC0);   // 0x20: user   data 32-bit DPL=3

    // TSS entry (selector 0x28) — 32-bit TSS, DPL=0, present
    // Initialize TSS: esp0 will be set per-task on context switch
    for (int i = 0; i < (int)sizeof(kernel_tss32); i++)
        ((uint8_t *)&kernel_tss32)[i] = 0;
    kernel_tss32.ss0       = 0x10;     // kernel data segment
    kernel_tss32.iomap_base = sizeof(kernel_tss32);  // no I/O bitmap

    uint32_t tss_base  = (uint32_t)&kernel_tss32;
    uint32_t tss_limit = sizeof(kernel_tss32) - 1;
    // access byte: P=1,DPL=0,0,type=10B1 (32-bit TSS available) = 0x89
    gdt32_set(5, tss_base, tss_limit, 0x89, 0x00);

    gdt32_ptr.limit = sizeof(gdt32) - 1;
    gdt32_ptr.base  = (uint32_t)gdt32;

    __asm__ volatile("lgdt %0" :: "m"(gdt32_ptr));

    // Reload CS via far return (push new CS then EIP label, retf)
    __asm__ volatile(
        "push $0x08\n\t"
        "push $1f\n\t"
        "retf\n\t"
        "1:\n\t"
        ::: "memory"
    );

    // Reload data segments
    __asm__ volatile(
        "mov $0x10, %%ax\n\t"
        "mov %%ax,  %%ds\n\t"
        "mov %%ax,  %%es\n\t"
        "mov %%ax,  %%fs\n\t"
        "mov %%ax,  %%gs\n\t"
        "mov %%ax,  %%ss\n\t"
        ::: "ax"
    );

    // Load the Task Register with our TSS selector (0x28)
    __asm__ volatile("ltr %%ax" :: "a"(0x28));
}

// =============================================================================
// IDT — 256 entries (32-bit, 8-byte gate)
// =============================================================================

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  reserved;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) IdtEntry32;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) IdtPointer32;

static IdtEntry32 idt32[256];
static IdtPointer32 idt32_ptr;

// Forward declarations for ISR/IRQ stubs
extern void isr0(void); extern void isr1(void);  extern void isr2(void);
extern void isr3(void); extern void isr4(void);  extern void isr5(void);
extern void isr6(void); extern void isr7(void);  extern void isr8(void);
extern void isr9(void); extern void isr10(void); extern void isr11(void);
extern void isr12(void);extern void isr13(void); extern void isr14(void);
extern void isr15(void);extern void isr16(void); extern void isr17(void);
extern void isr18(void);extern void isr19(void); extern void isr20(void);
extern void isr21(void);extern void isr22(void); extern void isr23(void);
extern void isr24(void);extern void isr25(void); extern void isr26(void);
extern void isr27(void);extern void isr28(void); extern void isr29(void);
extern void isr30(void);extern void isr31(void);

extern void irq0(void); extern void irq1(void);  extern void irq2(void);
extern void irq3(void); extern void irq4(void);  extern void irq5(void);
extern void irq6(void); extern void irq7(void);  extern void irq8(void);
extern void irq9(void); extern void irq10(void); extern void irq11(void);
extern void irq12(void);extern void irq13(void); extern void irq14(void);
extern void irq15(void);

extern void isr_syscall(void);

static void idt32_set(uint8_t vec, void (*handler)(void), uint8_t type_attr) {
    uint32_t addr = (uint32_t)handler;
    idt32[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt32[vec].selector    = 0x08;          // kernel code selector
    idt32[vec].reserved    = 0;
    idt32[vec].type_attr   = type_attr;
    idt32[vec].offset_high = (uint16_t)(addr >> 16);
}

static void idt_init(void) {
    // Zero entire IDT
    for (int i = 0; i < 256; i++) {
        idt32[i].offset_low = idt32[i].offset_high = 0;
        idt32[i].selector   = 0;
        idt32[i].reserved   = 0;
        idt32[i].type_attr  = 0;
    }
    // 0x8E = interrupt gate, 32-bit, DPL=0
    idt32_set(0,  isr0,  0x8E); idt32_set(1,  isr1,  0x8E);
    idt32_set(2,  isr2,  0x8E); idt32_set(3,  isr3,  0x8E);
    idt32_set(4,  isr4,  0x8E); idt32_set(5,  isr5,  0x8E);
    idt32_set(6,  isr6,  0x8E); idt32_set(7,  isr7,  0x8E);
    idt32_set(8,  isr8,  0x8E); idt32_set(9,  isr9,  0x8E);
    idt32_set(10, isr10, 0x8E); idt32_set(11, isr11, 0x8E);
    idt32_set(12, isr12, 0x8E); idt32_set(13, isr13, 0x8E);
    idt32_set(14, isr14, 0x8E); idt32_set(15, isr15, 0x8E);
    idt32_set(16, isr16, 0x8E); idt32_set(17, isr17, 0x8E);
    idt32_set(18, isr18, 0x8E); idt32_set(19, isr19, 0x8E);
    idt32_set(20, isr20, 0x8E); idt32_set(21, isr21, 0x8E);
    idt32_set(22, isr22, 0x8E); idt32_set(23, isr23, 0x8E);
    idt32_set(24, isr24, 0x8E); idt32_set(25, isr25, 0x8E);
    idt32_set(26, isr26, 0x8E); idt32_set(27, isr27, 0x8E);
    idt32_set(28, isr28, 0x8E); idt32_set(29, isr29, 0x8E);
    idt32_set(30, isr30, 0x8E); idt32_set(31, isr31, 0x8E);
    // Hardware IRQ stubs
    idt32_set(0x20, irq0,  0x8E); idt32_set(0x21, irq1,  0x8E);
    idt32_set(0x22, irq2,  0x8E); idt32_set(0x23, irq3,  0x8E);
    idt32_set(0x24, irq4,  0x8E); idt32_set(0x25, irq5,  0x8E);
    idt32_set(0x26, irq6,  0x8E); idt32_set(0x27, irq7,  0x8E);
    idt32_set(0x28, irq8,  0x8E); idt32_set(0x29, irq9,  0x8E);
    idt32_set(0x2A, irq10, 0x8E); idt32_set(0x2B, irq11, 0x8E);
    idt32_set(0x2C, irq12, 0x8E); idt32_set(0x2D, irq13, 0x8E);
    idt32_set(0x2E, irq14, 0x8E); idt32_set(0x2F, irq15, 0x8E);
    // INT 0x80 syscall — trap gate DPL=3 (0xEF) so user code can invoke it
    idt32_set(0x80, isr_syscall, 0xEF);

    idt32_ptr.limit = sizeof(idt32) - 1;
    idt32_ptr.base  = (uint32_t)idt32;
    __asm__ volatile("lidt %0" :: "m"(idt32_ptr));
}

// =============================================================================
// PIC 8259A remap
// =============================================================================

static void pic_init(void) {
    // ICW1: cascade init
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    // ICW2: vector offsets
    outb(0x21, 0x20); io_wait();   // master IRQ0-7 → 0x20-0x27
    outb(0xA1, 0x28); io_wait();   // slave  IRQ8-15 → 0x28-0x2F
    // ICW3
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    // ICW4: 8086 mode
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    // mask: enable only IRQ0 (timer) and IRQ1 (keyboard), mask the rest
    // If framebuffer mode, also unmask IRQ2 (cascade) and IRQ12 (mouse)
    if (display_mode >= 1) {
        outb(0x21, 0xF8);  // 11111000 = unmask IRQ0, IRQ1, IRQ2(cascade)
        outb(0xA1, 0xEF);  // 11101111 = unmask IRQ12 (mouse)
    } else {
        outb(0x21, 0xFC);  // 11111100 = mask all except IRQ0, IRQ1
        outb(0xA1, 0xFF);  // mask all slave IRQs
    }
}

// =============================================================================
// PIT timer — 240 Hz
// =============================================================================

static volatile uint32_t kernel_tick = 0;

#define PIT_FREQUENCY 1193182UL
#define TIMER_HZ 240
#define SYS_USER_TEST 0xF0

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

#define USER_VADDR_MIN_32 0x10000000U
#define USER_VADDR_MAX_32 0x40000000U
#define SYS_IO_PATH_MAX   64U
#define SYS_IO_BUF_MAX    4096U

static int user_ptr_range_valid_32(uint32_t addr, uint32_t len) {
    if (addr == 0 || len == 0) return 0;
    uint32_t end = addr + len;
    if (end < addr) return 0;
    return addr >= USER_VADDR_MIN_32 && end <= USER_VADDR_MAX_32;
}

static int copy_user_path_32(char *dst, uint32_t user_ptr) {
    if (!dst || user_ptr == 0) return -1;
    if (user_ptr < USER_VADDR_MIN_32 || user_ptr >= USER_VADDR_MAX_32) return -1;

    const char *src = (const char *)(uintptr_t)user_ptr;
    for (uint32_t i = 0; i < SYS_IO_PATH_MAX; i++) {
        char c = src[i];
        dst[i] = c;
        if (c == '\0') return 0;
    }
    dst[SYS_IO_PATH_MAX - 1] = '\0';
    return -1;
}

static int32_t syscall_vfs_read_32(uint32_t path_ptr, uint32_t user_buf_ptr, uint32_t max_len) {
    if (max_len == 0 || max_len > SYS_IO_BUF_MAX) return -1;
    if (!user_ptr_range_valid_32(user_buf_ptr, max_len)) return -1;

    char path[SYS_IO_PATH_MAX];
    if (copy_user_path_32(path, path_ptr) < 0) return -1;

    static uint8_t kbuf[SYS_IO_BUF_MAX];
    int n = kfs_read_file(path, kbuf, max_len);
    if (n < 0) return -1;

    uint8_t *dst = (uint8_t *)(uintptr_t)user_buf_ptr;
    for (int i = 0; i < n; i++) dst[i] = kbuf[i];
    return n;
}

static int32_t syscall_vfs_write_32(uint32_t path_ptr, uint32_t user_buf_ptr, uint32_t len) {
    if (len == 0 || len > SYS_IO_BUF_MAX) return -1;
    if (!user_ptr_range_valid_32(user_buf_ptr, len)) return -1;

    char path[SYS_IO_PATH_MAX];
    if (copy_user_path_32(path, path_ptr) < 0) return -1;

    static uint8_t kbuf[SYS_IO_BUF_MAX];
    const uint8_t *src = (const uint8_t *)(uintptr_t)user_buf_ptr;
    for (uint32_t i = 0; i < len; i++) kbuf[i] = src[i];

    return kfs_write_file(path, kbuf, len);
}

static void pit_init(void) {
    // channel 0, lo/hi byte, mode 3 (square wave)
    outb(0x43, 0x36);
    uint16_t divisor = (uint16_t)(PIT_FREQUENCY / TIMER_HZ);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
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

    if (!(regB & 0x04)) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hr  = bcd_to_bin(hr & 0x7F);
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        yr  = bcd_to_bin(yr);
    }

    if (!(regB & 0x02) && (cmos_read(0x04) & 0x80)) {
        hr = ((hr & 0x7F) + 12) % 24;
    }

    t->second = sec;
    t->minute = min;
    t->hour   = hr;
    t->day    = day;
    t->month  = mon;
    t->year   = 2000 + yr;
}

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
// PS/2 Keyboard — IRQ1
// =============================================================================

static const char scancode_table[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0, 0, 0,   // F1-F10, Num, Scroll
    '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0,
    0,0,
};

static const char scancode_table_shift[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0, 0, 0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0,
    0,0,
};

typedef struct {
    char    buf[256];
    uint8_t read_pos;
    uint8_t write_pos;
    uint8_t shift_held;
    uint8_t caps_lock;
    uint8_t extended;   // 0xE0 prefix received
} KbdState;

static KbdState kbd;

// Phase 42: forward declarations (defined after InterruptFrame32)
typedef struct KernelTTY32_ KernelTTY32;
static void tty_push_char_32(KernelTTY32 *tty, char c);
static KernelTTY32 kernel_tty0_32;

static void keyboard_init(void) {
    kbd.read_pos = kbd.write_pos = 0;
    kbd.shift_held = kbd.caps_lock = 0;
}

static void keyboard_irq_handler(void) {
    uint8_t sc = inb(0x60);

    // Extended key prefix (0xE0) — next scancode is an extended key
    if (sc == 0xE0) { kbd.extended = 1; return; }

    if (kbd.extended) {
        kbd.extended = 0;
        if (sc & 0x80) return;  // extended key release — ignore
        char code = 0;
        switch (sc) {
            case 0x48: code = (char)0x80; break;  // Up
            case 0x50: code = (char)0x81; break;  // Down
            case 0x4B: code = (char)0x82; break;  // Left
            case 0x4D: code = (char)0x83; break;  // Right
            case 0x53: code = (char)0x84; break;  // Delete
            case 0x47: code = (char)0x85; break;  // Home
            case 0x4F: code = (char)0x86; break;  // End
        }
        if (code) kbd.buf[kbd.write_pos++] = code;
        return;
    }

    if (sc & 0x80) {  // key release
        uint8_t key = sc & 0x7F;
        if (key == 0x2A || key == 0x36) kbd.shift_held = 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) { kbd.shift_held = 1; return; }
    if (sc == 0x3A) { kbd.caps_lock ^= 1; return; }

    char c;
    if (kbd.shift_held) c = scancode_table_shift[sc];
    else                c = scancode_table[sc];

    if (c >= 'a' && c <= 'z' && kbd.caps_lock) c = (char)(c - 32);
    if (c) kbd.buf[kbd.write_pos++] = c;
    // Phase 42: also push to TTY for user-space stdin
    if (c) tty_push_char_32(&kernel_tty0_32, c);
}

// In GUI mode, keyboard IRQs are routed to GUI event handling.
// Mirror a minimal set of control keys into the CLI buffer so commands that
// poll keyboard_read_char() (like `ps` realtime) can still exit.
static void keyboard_feed_cli_hotkeys(uint8_t sc) {
    if (sc & 0x80) return;  // key release

    char c = 0;
    if (sc == 0x10) c = 'q';        // Q key
    else if (sc == 0x1C) c = '\n';  // Enter
    else if (sc == 0x01) c = 27;    // Esc

    if (c) kbd.buf[kbd.write_pos++] = c;
}

int keyboard_read_char(char *out) {
    if (kbd.read_pos == kbd.write_pos) return 0;
    *out = kbd.buf[kbd.read_pos++];
    return 1;
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
        break;
    }
}

// =============================================================================
// Interrupt dispatch
// =============================================================================

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, error_code;
    uint32_t eip, cs, eflags;
    uint32_t user_esp, user_ss;   // only valid on ring-3 → ring-0
} __attribute__((packed)) InterruptFrame32;

// Forward declarations — defined later or in Rust FFI
extern int32_t syscall_handler(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3);
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
// Phase 41: Per-process file descriptor table (x86)
// =============================================================================
#define FD_MAX            16
#define FD_TYPE_NONE       0
#define FD_TYPE_FILE       1
#define FD_TYPE_TTY        2
#define FD_TYPE_PIPE_R     3
#define FD_TYPE_PIPE_W     4

// Phase 42: Kernel TTY (x86)
#define TTY_BUF_SIZE     256

typedef struct KernelTTY32_ {
    char     input_buf[TTY_BUF_SIZE];
    uint32_t input_read;
    uint32_t input_write;
    uint32_t input_count;
    uint8_t  cooked;
    char     line_buf[TTY_BUF_SIZE];
    uint32_t line_len;
    uint8_t  line_ready;
} KernelTTY32;

static void tty_init_32(KernelTTY32 *tty) {
    for (int i = 0; i < TTY_BUF_SIZE; i++) {
        tty->input_buf[i] = 0; tty->line_buf[i] = 0;
    }
    tty->input_read = tty->input_write = tty->input_count = 0;
    tty->cooked = 1; tty->line_len = 0; tty->line_ready = 0;
}

static void tty_push_char_32(KernelTTY32 *tty, char c) {
    if (tty->cooked) {
        if (c == '\n' || c == '\r') {
            for (uint32_t i = 0; i < tty->line_len && tty->input_count < TTY_BUF_SIZE; i++) {
                tty->input_buf[tty->input_write] = tty->line_buf[i];
                tty->input_write = (tty->input_write + 1) % TTY_BUF_SIZE;
                tty->input_count++;
            }
            if (tty->input_count < TTY_BUF_SIZE) {
                tty->input_buf[tty->input_write] = '\n';
                tty->input_write = (tty->input_write + 1) % TTY_BUF_SIZE;
                tty->input_count++;
            }
            tty->line_len = 0; tty->line_ready = 1;
        } else if (c == '\b' || c == 127) {
            if (tty->line_len > 0) tty->line_len--;
        } else if (c == 3) {
            tty->line_len = 0;
        } else {
            if (tty->line_len < TTY_BUF_SIZE - 1)
                tty->line_buf[tty->line_len++] = c;
        }
    } else {
        if (tty->input_count < TTY_BUF_SIZE) {
            tty->input_buf[tty->input_write] = c;
            tty->input_write = (tty->input_write + 1) % TTY_BUF_SIZE;
            tty->input_count++;
        }
    }
}

static int tty_read_32(KernelTTY32 *tty, char *buf, int max) {
    int n = 0;
    while (n < max && tty->input_count > 0) {
        buf[n++] = tty->input_buf[tty->input_read];
        tty->input_read = (tty->input_read + 1) % TTY_BUF_SIZE;
        tty->input_count--;
    }
    if (tty->cooked && tty->input_count == 0) tty->line_ready = 0;
    return n;
}

static int tty_write_32(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        terminal_putchar(buf[i]);
        serial_putchar(buf[i]);
    }
    return len;
}

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
} KernelPipe32;

static KernelPipe32 kernel_pipes_32[MAX_PIPES];

typedef struct {
    uint8_t  type;
    uint8_t  flags;
    char     path[64];
    uint32_t offset;
    uint8_t  pipe_idx;
} FdEntry32;

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

#define MMAP_BASE_32  0x30000000U

typedef struct {
    uint32_t start;
    uint32_t length;
    uint8_t  type;       // VMA_TYPE_*
    uint8_t  prot;       // PROT_READ | PROT_WRITE | PROT_EXEC
    uint8_t  flags;      // MAP_ANONYMOUS | MAP_PRIVATE
    uint8_t  _pad;
    char     path[64];   // file-backed: VFS path
    uint32_t file_offset;
} VmaEntry32;

typedef struct {
    uint32_t esp;                  // saved stack pointer (into stack[])
    uint32_t pid;                  // associated scheduler PID
    uint8_t  active;               // 1 = slot in use
    uint16_t ticks_remaining;
    uint16_t ticks_total;
    FdEntry32 fd_table[FD_MAX];
    uint32_t ppid_slot;
    uint32_t brk;
    VmaEntry32 vma_list[VMA_MAX_PER_TASK]; // Phase 46: mmap regions
    uint32_t mmap_next;            // Phase 46: next mmap virtual address
    uint8_t  stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
} TaskSlot32;

static TaskSlot32 task_slots[MAX_TASKS];
static int        current_task_idx      = -1;
static int        context_switch_enabled = 0;
static uint32_t   context_switch_count  = 0;

// Forward declaration for elf_exec_32 (used by sys_execve_32)
static int elf_exec_32(const char *path, uint16_t ticks);

// =============================================================================
// Phase 41: File descriptor helpers (x86)
// =============================================================================

static void fd_table_init_32(int slot) {
    for (int i = 0; i < FD_MAX; i++)
        task_slots[slot].fd_table[i].type = FD_TYPE_NONE;
    // fd 0 = stdin (TTY read), fd 1 = stdout, fd 2 = stderr
    task_slots[slot].fd_table[0].type = FD_TYPE_TTY;
    task_slots[slot].fd_table[0].flags = 0; // read
    task_slots[slot].fd_table[1].type = FD_TYPE_TTY;
    task_slots[slot].fd_table[1].flags = 1; // write
    task_slots[slot].fd_table[2].type = FD_TYPE_TTY;
    task_slots[slot].fd_table[2].flags = 1; // write
}

static int fd_alloc_32(int slot) {
    for (int i = 0; i < FD_MAX; i++)
        if (task_slots[slot].fd_table[i].type == FD_TYPE_NONE) return i;
    return -1;
}

static void fd_close_entry_32(FdEntry32 *e) {
    if (e->type == FD_TYPE_PIPE_R && e->pipe_idx < MAX_PIPES)
        kernel_pipes_32[e->pipe_idx].read_open = 0;
    if (e->type == FD_TYPE_PIPE_W && e->pipe_idx < MAX_PIPES)
        kernel_pipes_32[e->pipe_idx].write_open = 0;
    e->type = FD_TYPE_NONE;
}

static void fd_copy_32(int src_slot, int dst_slot) {
    for (int i = 0; i < FD_MAX; i++)
        task_slots[dst_slot].fd_table[i] = task_slots[src_slot].fd_table[i];
}

static int32_t sys_open_32(uint32_t path_ptr, uint32_t flags) {
    if (current_task_idx < 0) return -1;
    int fd = fd_alloc_32(current_task_idx);
    if (fd < 0) return -1;
    char path[64];
    if (copy_user_path_32(path, path_ptr) < 0) return -1;
    FdEntry32 *e = &task_slots[current_task_idx].fd_table[fd];
    e->type = FD_TYPE_FILE;
    e->flags = (uint8_t)flags;
    e->offset = 0;
    for (int i = 0; i < 63 && path[i]; i++) { e->path[i] = path[i]; e->path[i+1] = 0; }
    return fd;
}

static int32_t sys_read_fd_32(uint32_t fd_num, uint32_t buf_ptr, uint32_t count) {
    if (current_task_idx < 0) return -1;
    if (fd_num >= FD_MAX) return -1;
    if (!user_ptr_range_valid_32(buf_ptr, count)) return -1;
    FdEntry32 *e = &task_slots[current_task_idx].fd_table[fd_num];
    if (e->type == FD_TYPE_NONE) return -1;
    if (e->type == FD_TYPE_TTY)
        return tty_read_32(&kernel_tty0_32, (char *)buf_ptr, (int)count);
    if (e->type == FD_TYPE_PIPE_R) {
        if (e->pipe_idx >= MAX_PIPES) return -1;
        KernelPipe32 *p = &kernel_pipes_32[e->pipe_idx];
        int n = 0;
        while (n < (int)count && p->count > 0) {
            ((char *)buf_ptr)[n++] = p->buf[p->read_pos];
            p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
            p->count--;
        }
        return n;
    }
    if (e->type == FD_TYPE_FILE) {
        static uint8_t file_tmp[4096];
        int sz = kfs_read_file(e->path, file_tmp, sizeof(file_tmp));
        if (sz < 0) return -1;
        int avail = sz - (int)e->offset;
        if (avail <= 0) return 0;
        int n = (avail < (int)count) ? avail : (int)count;
        for (int i = 0; i < n; i++) ((uint8_t *)buf_ptr)[i] = file_tmp[e->offset + i];
        e->offset += n;
        return n;
    }
    return -1;
}

static int32_t sys_write_fd_32(uint32_t fd_num, uint32_t buf_ptr, uint32_t count) {
    if (current_task_idx < 0) return -1;
    if (fd_num >= FD_MAX) return -1;
    if (!user_ptr_range_valid_32(buf_ptr, count)) return -1;
    FdEntry32 *e = &task_slots[current_task_idx].fd_table[fd_num];
    if (e->type == FD_TYPE_NONE) return -1;
    if (e->type == FD_TYPE_TTY)
        return tty_write_32((const char *)buf_ptr, (int)count);
    if (e->type == FD_TYPE_PIPE_W) {
        if (e->pipe_idx >= MAX_PIPES) return -1;
        KernelPipe32 *p = &kernel_pipes_32[e->pipe_idx];
        int n = 0;
        while (n < (int)count && p->count < PIPE_BUF_SIZE) {
            p->buf[p->write_pos] = ((const char *)buf_ptr)[n++];
            p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
            p->count++;
        }
        return n;
    }
    return -1;
}

static int32_t sys_close_32(uint32_t fd_num) {
    if (current_task_idx < 0 || fd_num >= FD_MAX) return -1;
    FdEntry32 *e = &task_slots[current_task_idx].fd_table[fd_num];
    if (e->type == FD_TYPE_NONE) return -1;
    fd_close_entry_32(e);
    return 0;
}

static int32_t sys_dup_32(uint32_t old_fd) {
    if (current_task_idx < 0 || old_fd >= FD_MAX) return -1;
    if (task_slots[current_task_idx].fd_table[old_fd].type == FD_TYPE_NONE) return -1;
    int nfd = fd_alloc_32(current_task_idx);
    if (nfd < 0) return -1;
    task_slots[current_task_idx].fd_table[nfd] = task_slots[current_task_idx].fd_table[old_fd];
    return nfd;
}

static int32_t sys_dup2_32(uint32_t old_fd, uint32_t new_fd) {
    if (current_task_idx < 0 || old_fd >= FD_MAX || new_fd >= FD_MAX) return -1;
    if (task_slots[current_task_idx].fd_table[old_fd].type == FD_TYPE_NONE) return -1;
    if (old_fd == new_fd) return (int32_t)new_fd;
    fd_close_entry_32(&task_slots[current_task_idx].fd_table[new_fd]);
    task_slots[current_task_idx].fd_table[new_fd] = task_slots[current_task_idx].fd_table[old_fd];
    return (int32_t)new_fd;
}

static int32_t sys_pipe_32(uint32_t fds_ptr) {
    if (current_task_idx < 0) return -1;
    if (!user_ptr_range_valid_32(fds_ptr, 8)) return -1;
    int pi = -1;
    for (int i = 0; i < MAX_PIPES; i++)
        if (!kernel_pipes_32[i].active) { pi = i; break; }
    if (pi < 0) return -1;
    int rfd = fd_alloc_32(current_task_idx);
    if (rfd < 0) return -1;
    int wfd = -1;
    for (int i = rfd + 1; i < FD_MAX; i++)
        if (task_slots[current_task_idx].fd_table[i].type == FD_TYPE_NONE) { wfd = i; break; }
    if (wfd < 0) { return -1; }
    KernelPipe32 *p = &kernel_pipes_32[pi];
    p->read_pos = p->write_pos = p->count = 0;
    p->active = p->read_open = p->write_open = 1;
    task_slots[current_task_idx].fd_table[rfd].type = FD_TYPE_PIPE_R;
    task_slots[current_task_idx].fd_table[rfd].pipe_idx = (uint8_t)pi;
    task_slots[current_task_idx].fd_table[wfd].type = FD_TYPE_PIPE_W;
    task_slots[current_task_idx].fd_table[wfd].pipe_idx = (uint8_t)pi;
    ((int32_t *)fds_ptr)[0] = rfd;
    ((int32_t *)fds_ptr)[1] = wfd;
    return 0;
}

// Phase 43: fork/execve/sbrk — defined after ELF types (see below elf_exec_32)
static int32_t sys_fork_32(InterruptFrame32 *frame);
static int32_t sys_execve_32(InterruptFrame32 *frame, uint32_t path_ptr);
static int32_t sys_sbrk_32(int32_t incr);

static inline uint32_t read_cr2_32(void) {
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

// Forward declarations for Phase 46 demand paging (used in interrupt_dispatch)
#ifndef PAGE_PRESENT_32
#define PAGE_PRESENT_32  0x01
#define PAGE_WRITABLE_32 0x02
#define PAGE_USER_32     0x04
#define PAGE_PS_32       0x80
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
static uint32_t frame_alloc_32(void);
static void paging_map_4k_32(uint32_t *pd, uint32_t virt, uint32_t phys, uint32_t flags);
static void paging_flush_tlb_32(void);
static uint32_t kernel_page_dir[1024] __attribute__((aligned(4096)));
static uint8_t mmap_file_tmp_32[65536];
static VmaEntry32 *vma_find_32(VmaEntry32 *vmas, uint32_t addr);
static void vma_init_32(VmaEntry32 *vmas);
static int32_t sys_mmap_32(uint32_t length, uint32_t prot_flags, uint32_t path_ptr);
static int32_t sys_munmap_32(uint32_t addr, uint32_t length);

static void log_page_fault_detail_32(uint32_t error_code) {
    uint32_t fault_addr = read_cr2_32();
    uint8_t present = (uint8_t)(error_code & 0x1);
    uint8_t write = (uint8_t)((error_code >> 1) & 0x1);
    uint8_t user = (uint8_t)((error_code >> 2) & 0x1);
    uint8_t rsvd = (uint8_t)((error_code >> 3) & 0x1);
    uint8_t ifetch = (uint8_t)((error_code >> 4) & 0x1);

    serial_print("[PF] cr2=");
    serial_print_hex(fault_addr);
    serial_print(" present=");
    serial_print_dec(present);
    serial_print(" write=");
    serial_print_dec(write);
    serial_print(" user=");
    serial_print_dec(user);
    serial_print(" rsvd=");
    serial_print_dec(rsvd);
    serial_print(" ifetch=");
    serial_print_dec(ifetch);
    serial_print("\n");

    terminal_writestring("[PF] addr=");
    vga_print_hex(fault_addr);
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

uint32_t interrupt_dispatch(InterruptFrame32 *frame) {
    uint32_t n = frame->int_no;

    if (n < 32) {
        // CPU exception
        serial_print("[EXCEPTION] vec=");
        serial_print_hex(n);
        serial_print(" err=");
        serial_print_hex(frame->error_code);
        serial_print(" eip=");
        serial_print_hex(frame->eip);
        serial_print("\n");
        terminal_writestring("[EXCEPTION] ");

        if (n == 14) {
            log_page_fault_detail_32(frame->error_code);
        } else if (n == 13) {
            serial_print("[GP] general protection fault detected\n");
        } else if (n == 8) {
            serial_print("[DF] double fault detected\n");
        }

        // Phase 10: Notify AI of CPU exception
        {
            char evbuf[48];
            char vecbuf[12]; int i = 0; char tmp[12]; uint32_t v = n;
            if (v == 0) { tmp[i++] = '0'; } else { while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; } }
            int p = 0; for (int j = i - 1; j >= 0; j--) vecbuf[p++] = tmp[j]; vecbuf[p] = '\0';
            char eipbuf[12]; eipbuf[0] = '0'; eipbuf[1] = 'x';
            uint32_t r = frame->eip;
            for (int k = 7; k >= 0; k--) {
                uint8_t nibble = (r >> (k * 4)) & 0xF;
                eipbuf[9 - k] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
            }
            eipbuf[10] = '\0';
            ai_build_event(evbuf, sizeof(evbuf), vecbuf, eipbuf, (void*)0);
            ai_send_event(AI_EVT_EXCEPTION, evbuf);
            ai_kernel_engine_feed(AI_EVT_EXCEPTION, evbuf, kernel_tick);        }

        if ((frame->cs & 0x3) == 0x3) {
            // Phase 46: demand paging — check VMAs before killing
            if (n == 14 && current_task_idx >= 0) {
                uint32_t fault_addr;
                __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
                TaskSlot32 *t = &task_slots[current_task_idx];
                VmaEntry32 *vma = vma_find_32(t->vma_list, fault_addr);
                if (vma) {
                    uint32_t page_va = fault_addr & ~0xFFFU;
                    uint32_t fr = frame_alloc_32();
                    if (fr) {
                        uint32_t pg_flags = PAGE_USER_32;
                        if (vma->prot & PROT_WRITE) pg_flags |= PAGE_WRITABLE_32;

                        if (vma->type == VMA_TYPE_FILE && vma->path[0]) {
                            uint32_t page_off = (page_va - vma->start) + vma->file_offset;
                            uint32_t need = page_off + PAGE_SIZE;
                            if (need > sizeof(mmap_file_tmp_32)) need = sizeof(mmap_file_tmp_32);
                            int total = kfs_read_file(vma->path, mmap_file_tmp_32, need);
                            if (total > (int)page_off) {
                                int ncp = total - (int)page_off;
                                if (ncp > PAGE_SIZE) ncp = PAGE_SIZE;
                                uint8_t *dst = (uint8_t *)fr;
                                for (int b = 0; b < ncp; b++)
                                    dst[b] = mmap_file_tmp_32[page_off + b];
                            }
                        }

                        paging_map_4k_32(kernel_page_dir, page_va, fr, pg_flags);
                        paging_flush_tlb_32();
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
                    serial_print_dec(pid);
                    serial_print("\n");
                    (void)scheduler_kill_process(sched, pid);
                    (void)scheduler_schedule(sched);
                }
            }
            // Deactivate the faulting task slot and switch to next
            if (context_switch_enabled && current_task_idx >= 0) {
                task_slots[current_task_idx].active = 0;
                int next = -1;
                for (int i = 0; i < MAX_TASKS; i++) {
                    int idx = (current_task_idx + 1 + i) % MAX_TASKS;
                    if (task_slots[idx].active) { next = idx; break; }
                }
                if (next >= 0) {
                    serial_print("[EXCEPTION] switching to task ");
                    serial_print_dec(next);
                    serial_print("\n");
                    current_task_idx = next;
                    return task_slots[next].esp;
                }
            }
            serial_print("[EXCEPTION] no task to switch to; halting\n");
        }

        __asm__ volatile("cli");
        while (1) __asm__ volatile("hlt");
        return 0;  // unreachable
    }

    if (n == 0x20) {   // IRQ0: timer — keep FAST: no Rust/heap calls from IRQ
        kernel_tick++;
        outb(0x20, 0x20);  // EOI first so keyboard IRQ is not delayed

        // COM2 polling is simple port I/O — safe in IRQ
        const uint32_t cmd_poll_div = (TIMER_HZ / 10) ? (TIMER_HZ / 10) : 1;
        if ((kernel_tick % cmd_poll_div) == 0) {
            ai_poll_cmd();
        }

        // Phase 48: periodic dirty-block writeback
        bcache_tick();

        // Phase 18: preemptive context switch (pure C, no Rust calls)
        if (context_switch_enabled && current_task_idx >= 0) {
            TaskSlot32 *cur = &task_slots[current_task_idx];
            if (cur->ticks_remaining > 0)
                cur->ticks_remaining--;
            if (cur->ticks_remaining == 0) {
                cur->esp             = (uint32_t)frame;
                cur->ticks_remaining = cur->ticks_total;

                int next = current_task_idx;
                for (int tries = 0; tries < MAX_TASKS; tries++) {
                    next = (next + 1) % MAX_TASKS;
                    if (task_slots[next].active) break;
                }
                if (next != current_task_idx && task_slots[next].active) {
                    context_switch_count++;
                    current_task_idx = next;
                    // Update TSS esp0 so Ring 3→Ring 0 transitions
                    // land on the correct per-task kernel stack.
                    kernel_tss32.esp0 = (uint32_t)(task_slots[next].stack + TASK_STACK_SIZE);
                    return task_slots[next].esp;   // asm switches ESP
                }
            }
        }
        return 0;
    }
    if (n == 0x21) {   // IRQ1: keyboard
        if (display_mode == 2) {
            uint8_t sc = inb(0x60);
            gui_handle_key(sc, (sc & 0x80) ? 0 : 1);
            keyboard_feed_cli_hotkeys(sc);
        } else {
            keyboard_irq_handler();
        }
        outb(0x20, 0x20);
        return 0;
    }
    if (n == 0x2C) {   // IRQ12: PS/2 Mouse
        mouse_irq_handler();
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
        return 0;
    }
    if (n >= 0x20 && n <= 0x2F) {
        if (n >= 0x28) outb(0xA0, 0x20);
        outb(0x20, 0x20);
        return 0;
    }
    if (n == 0x80) {   // INT 0x80 syscall
        uint32_t num = frame->eax;
        uint32_t a1  = frame->ebx;
        uint32_t a2  = frame->ecx;
        uint32_t a3  = frame->edx;
        int32_t  ret;
        if (num == SYS_USER_TEST) {
            // Phase 17: Ring 3 user heartbeat — print once per second
            static uint32_t user_test_count = 0;
            user_test_count++;
            if ((user_test_count % TIMER_HZ) == 1) {
                serial_print("[ring3] user heartbeat #");
                serial_print_dec(user_test_count);
                serial_print("\n");
            }
            ret = 0;
        } else if (num == SYS_EXIT) {
            // Phase 20: exit(exit_code) — terminate current process
            uint32_t exit_pid = 0;
            if (current_task_idx >= 0)
                exit_pid = task_slots[current_task_idx].pid;
            void *sched = get_kernel_scheduler();
            if (sched)
                scheduler_terminate_current(sched, (int32_t)a1);
            if (context_switch_enabled && current_task_idx >= 0) {
                task_slots[current_task_idx].active = 0;
                int next = -1;
                for (int i = 0; i < MAX_TASKS; i++) {
                    int idx = (current_task_idx + 1 + i) % MAX_TASKS;
                    if (task_slots[idx].active) { next = idx; break; }
                }
                serial_print("[exit] pid=");
                serial_print_dec(exit_pid);
                serial_print(" code=");
                serial_print_dec(a1);
                serial_print("\n");
                if (next >= 0) {
                    current_task_idx = next;
                    kernel_tss32.esp0 = (uint32_t)(task_slots[next].stack + TASK_STACK_SIZE);
                    return task_slots[next].esp;
                }
            }
            serial_print("[exit] no more tasks — halt\n");
            __asm__ volatile("cli; hlt");
            return 0;
        } else if (num == SYS_WAITPID) {
            // Phase 20: waitpid(pid) — non-blocking
            void *sched = get_kernel_scheduler();
            if (sched) {
                ret = scheduler_get_exit_code(sched, (size_t)a1);
            } else {
                ret = -1;
            }
        } else if (num == SYS_GETPID) {
            // Phase 20: getpid() — return current PID
            void *sched = get_kernel_scheduler();
            ret = sched ? (int32_t)scheduler_get_current_pid(sched) : 0;
        } else if (num == SYS_KILL) {
            // Phase 23: kill(pid, sig) — send signal to process
            uint32_t dst_pid = (uint32_t)a1;
            uint8_t sig = (uint8_t)a2;
            void *sched = get_kernel_scheduler();
            if (sched) {
                extern int scheduler_signal_send(void *sched, uint32_t dst_pid, uint8_t sig);
                ret = scheduler_signal_send(sched, dst_pid, sig);
            } else {
                ret = -1;
            }
        } else if (num == SYS_READ) {
            // Phase 25: read(path_ptr, user_buf_ptr, max_len) -> bytes read
            ret = syscall_vfs_read_32(a1, a2, a3);
        } else if (num == SYS_WRITE) {
            // Phase 25: write(path_ptr, user_buf_ptr, len) -> bytes written
            ret = syscall_vfs_write_32(a1, a2, a3);
        } else if (num == SYS_OPEN) {
            ret = sys_open_32(a1, a2);
        } else if (num == SYS_READ_FD) {
            ret = sys_read_fd_32(a1, a2, a3);
        } else if (num == SYS_WRITE_FD) {
            ret = sys_write_fd_32(a1, a2, a3);
        } else if (num == SYS_CLOSE) {
            ret = sys_close_32(a1);
        } else if (num == SYS_DUP) {
            ret = sys_dup_32(a1);
        } else if (num == SYS_DUP2) {
            ret = sys_dup2_32(a1, a2);
        } else if (num == SYS_PIPE) {
            ret = sys_pipe_32(a1);
        } else if (num == SYS_FORK) {
            ret = sys_fork_32(frame);
        } else if (num == SYS_EXECVE) {
            ret = sys_execve_32(frame, a1);
            if (ret == 0) return 0;  // execve succeeded — return directly
        } else if (num == SYS_SBRK) {
            ret = sys_sbrk_32((int32_t)a1);
        } else if (num == SYS_MMAP) {
            // Phase 46: mmap(length, prot_flags, path_ptr)
            ret = sys_mmap_32(a1, a2, a3);
        } else if (num == SYS_MUNMAP) {
            // Phase 46: munmap(addr, length)
            ret = sys_munmap_32(a1, a2);
        } else if (num == SYS_SYNC) {
            // Phase 48: sync() -> 0 or -1
            ret = (int32_t)bcache_sync();
        } else if (num >= SYS_IPC_SEND && num <= SYS_IPC_CHAN_CLOSE) {
            // Phase 4: IPC syscalls handled in C
            ret = ipc_syscall(num, a1, a2, a3);
        } else if (num >= SYS_MOD_LOAD && num <= SYS_MOD_INFO) {
            // Phase 5: Module syscalls handled in C
            ret = module_syscall(num, a1, a2, a3);
        } else if (num >= SYS_AI_QUERY && num <= SYS_AI_EVENT) {
            // Phase 8: AI syscalls handled in C
            ret = (int32_t)ai_syscall(num, a1, a2, a3);
        } else {
            // Phase 3: all other syscalls handled in Rust
            ret = syscall_handler(num, a1, a2, a3);
        }
        frame->eax = (uint32_t)ret;
        return 0;
    }
    return 0;
}

// =============================================================================
// Rust FFI
// =============================================================================

extern void verniskernel_init_heap(uint32_t start, uint32_t size);
extern void verniskernel_register_print(void (*cb)(const uint8_t *, uint32_t));
extern void syscall_init(void);
extern int32_t syscall_handler(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3);
extern void *scheduler_new(void);
extern uint32_t scheduler_create_process(void *sched, uint8_t priority, const char *command);
extern uint32_t scheduler_schedule(void *sched);
extern uint32_t scheduler_get_process_count(const void *sched);
extern int scheduler_set_priority(void *sched, uint32_t pid, uint8_t priority);
extern void scheduler_set_quantum(void *sched, uint32_t quantum_ms);

// Global scheduler instance
static void *kernel_scheduler = (void *)0;
void *get_kernel_scheduler(void) { return kernel_scheduler; }
uint32_t kernel_get_ticks(void)  { return kernel_tick; }
uint32_t kernel_is_gui_mode(void) { return (display_mode == 2) ? 1u : 0u; }

// Serial write callback — registered into Rust so kernel_print() works
static void rust_print_cb(const uint8_t *ptr, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) serial_putchar((char)ptr[i]);
}

#define HEAP_SIZE (8 * 1024 * 1024)
static uint8_t kernel_heap[HEAP_SIZE] __attribute__((aligned(4096)));

// =============================================================================
// Phase 16: Paging — Identity Map + Physical Frame Allocator (x86)
// =============================================================================

#ifndef PAGE_SIZE
#define PAGE_SIZE        4096
#define PAGE_PRESENT_32  0x01
#define PAGE_WRITABLE_32 0x02
#define PAGE_USER_32     0x04
#define PAGE_PS_32       0x80   // 4MB page (PSE)
#endif

// Physical frame allocator — bump allocator starting after _kernel_end
static uint32_t phys_next_free;
static uint32_t phys_alloc_end;
static uint32_t phys_frames_used;

static uint32_t frame_alloc_32(void) {
    if (phys_next_free >= phys_alloc_end) {
        serial_print("[paging] FATAL: out of page frames\n");
        return 0;
    }
    uint32_t frame = phys_next_free;
    phys_next_free += PAGE_SIZE;
    phys_frames_used++;
    // Zero the frame
    volatile uint8_t *p = (volatile uint8_t *)frame;
    for (int i = 0; i < PAGE_SIZE; i++) p[i] = 0;
    return frame;
}

// kernel_page_dir defined earlier (forward declaration section)

// Map a 4KB page (for user space — Phase 17+).
static void paging_map_4k_32(uint32_t *pd, uint32_t virt, uint32_t phys, uint32_t flags) {
    int pd_idx = virt >> 22;
    int pt_idx = (virt >> 12) & 0x3FF;

    // Cannot convert a 4MB PSE entry to a page table
    if (pd[pd_idx] & PAGE_PS_32) return;

    if (!(pd[pd_idx] & PAGE_PRESENT_32)) {
        uint32_t frame = frame_alloc_32();
        if (!frame) return;
        pd[pd_idx] = frame | PAGE_PRESENT_32 | PAGE_WRITABLE_32 | flags;
    }
    uint32_t *pt = (uint32_t *)(pd[pd_idx] & 0xFFFFF000);
    pt[pt_idx] = (phys & 0xFFFFF000) | flags | PAGE_PRESENT_32;
}

// Create a new address space for a user process (copies kernel entries).
uint32_t *paging_create_address_space_32(void) {
    uint32_t frame = frame_alloc_32();
    if (!frame) return (uint32_t *)0;
    uint32_t *new_pd = (uint32_t *)frame;
    for (int i = 0; i < 1024; i++) new_pd[i] = kernel_page_dir[i];
    return new_pd;
}

// Flush TLB
static void paging_flush_tlb_32(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

// Public accessors
uint32_t *paging_get_kernel_pd(void) { return kernel_page_dir; }
uint32_t  paging_get_frames_used_32(void) { return phys_frames_used; }

// Initialize x86 paging with PSE 4MB pages for kernel identity mapping.
// Enables paging (CR0.PG) — must be called AFTER BSS zeroing.
static void paging_init_32(void) {
    extern char _kernel_end[];
    phys_next_free  = ((uint32_t)(uintptr_t)_kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    phys_alloc_end  = phys_next_free + (2 * 1024 * 1024);  // 2MB for page structs
    phys_frames_used = 0;

    // Identity-map first 128MB with 4MB PSE pages (32 entries)
    for (int i = 0; i < 32; i++) {
        kernel_page_dir[i] = ((uint32_t)i * 0x400000)
                           | PAGE_PRESENT_32 | PAGE_WRITABLE_32 | PAGE_PS_32;
    }
    // Clear remaining entries
    for (int i = 32; i < 1024; i++) {
        kernel_page_dir[i] = 0;
    }

    // Map framebuffer if present (4MB PSE pages)
    {
        volatile struct boot_info *bi = (volatile struct boot_info *)0x5300;
        if (bi->magic == 0x56424549 && bi->fb_type == 1) {
            uint32_t fb_phys = bi->fb_addr;
            uint32_t fb_size = bi->fb_pitch * bi->fb_height;
            uint32_t fb_base = fb_phys & 0xFFC00000;   // align to 4MB
            uint32_t fb_end  = (fb_phys + fb_size + 0x3FFFFF) & 0xFFC00000;
            for (uint32_t a = fb_base; a < fb_end; a += 0x400000) {
                int idx = a >> 22;
                kernel_page_dir[idx] = a | PAGE_PRESENT_32 | PAGE_WRITABLE_32 | PAGE_PS_32;
            }
            serial_print("[paging] framebuffer mapped ");
            serial_print_hex(fb_base);
            serial_print("-");
            serial_print_hex(fb_end);
            serial_print("\n");
        }
    }

    // Enable PSE (CR4 bit 4)
    {
        uint32_t cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1 << 4);  // PSE
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    }

    // Set CR3 to our page directory
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)(uintptr_t)kernel_page_dir) : "memory");

    // Enable paging (CR0 bit 31) + write-protect (CR0 bit 16)
    {
        uint32_t cr0;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= (1 << 31) | (1 << 16);  // PG + WP
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    }

    serial_print("[paging] enabled: 128MB identity (PSE 4MB pages)\n");
}

// =============================================================================
// Phase 18: Context Switch — helper functions (x86)
// =============================================================================

uint32_t kernel_get_context_switches(void) { return context_switch_count; }

static int task_find(uint32_t pid) {
    for (int i = 0; i < MAX_TASKS; i++)
        if (task_slots[i].active && task_slots[i].pid == pid) return i;
    return -1;
}

// Build a fake InterruptFrame32 on the task's stack so the first
// context-switch into it pops+iret as if it was a resumed task.
static int task_create_32(void (*entry)(void), uint32_t pid, uint16_t ticks) {
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++)
        if (!task_slots[i].active) { slot = i; break; }
    if (slot < 0) return -1;

    task_slots[slot].active          = 1;
    task_slots[slot].pid             = pid;
    task_slots[slot].ticks_remaining = ticks;
    task_slots[slot].ticks_total     = ticks;
    task_slots[slot].ppid_slot       = 0;
    task_slots[slot].brk             = 0;
    fd_table_init_32(slot);
    vma_init_32(task_slots[slot].vma_list);
    task_slots[slot].mmap_next       = 0;

    uint32_t *sp = (uint32_t*)(task_slots[slot].stack + TASK_STACK_SIZE);

    // CPU-pushed portion (same-privilege: only EIP/CS/EFLAGS)
    *(--sp) = 0x202;             // EFLAGS (IF=1)
    *(--sp) = 0x08;              // CS (kernel code)
    *(--sp) = (uint32_t)entry;   // EIP

    // ISR macro portion
    *(--sp) = 0;   // error_code
    *(--sp) = 0;   // int_no

    // pusha pushes: EAX ECX EDX EBX ESP EBP ESI EDI
    *(--sp) = 0;   // EAX
    *(--sp) = 0;   // ECX
    *(--sp) = 0;   // EDX
    *(--sp) = 0;   // EBX
    *(--sp) = 0;   // ESP (ignored by popa)
    *(--sp) = 0;   // EBP
    *(--sp) = 0;   // ESI
    *(--sp) = 0;   // EDI

    // Segment registers (push ds, es, fs, gs)
    *(--sp) = 0x10;  // DS
    *(--sp) = 0x10;  // ES
    *(--sp) = 0x10;  // FS
    *(--sp) = 0x10;  // GS  <-- ESP points here

    task_slots[slot].esp = (uint32_t)sp;
    return slot;
}

static void task_register_main_32(uint32_t pid, uint16_t ticks) {
    task_slots[0].active          = 1;
    task_slots[0].pid             = pid;
    task_slots[0].esp             = 0;   // saved on first preemption
    task_slots[0].ticks_remaining = ticks;
    task_slots[0].ticks_total     = ticks;
    current_task_idx = 0;
}

static void phase18_worker_entry_32(void) {
    volatile uint32_t local_tick = 0;
    while (1) {
        __asm__ volatile("hlt");
        local_tick++;
        if ((local_tick % TIMER_HZ) == 0) {
            serial_print("[worker] ctx_sw=");
            serial_print_dec(context_switch_count);
            serial_print("\n");
        }
    }
}

// =============================================================================
// Phase 17: Ring 3 user-mode task support
// =============================================================================

// User-mode virtual address layout — placed beyond the 128 MB identity region
// (32 × 4 MB PSE pages) so they don't collide with the kernel's huge-page mappings.
#define USER_CODE_VADDR_32  0x10000000U   // 256 MB — user .text page
#define USER_STACK_TOP_32   0x10800000U   // 264 MB — top of user stack
#define USER_STACK_SIZE_32  0x4000U       // 16 KB user stack

// Legacy phase17 heartbeat task helpers removed.
// User-space boot path is now shell-only via ELF loader (/bin/vsh32).

// =============================================================================
// Phase 19: ELF Loader (32-bit)
// =============================================================================

// ELF32 header structures
#define ELF_MAGIC 0x464C457FU   // "\x7FELF" little-endian

typedef struct {
    uint32_t e_magic;
    uint8_t  e_class;       // 1=32-bit, 2=64-bit
    uint8_t  e_data;        // 1=LE, 2=BE
    uint8_t  e_version;
    uint8_t  e_osabi;
    uint8_t  e_pad[8];
    uint16_t e_type;        // 2=ET_EXEC
    uint16_t e_machine;     // 3=EM_386
    uint32_t e_version2;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
    uint32_t p_type;        // 1=PT_LOAD
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

#define PT_LOAD  1
#define ET_EXEC  2
#define EM_386   3

#define ELF_BUF_SIZE  (128 * 1024)
static uint8_t elf_load_buf_32[ELF_BUF_SIZE] __attribute__((aligned(4096)));

// Load an ELF32 executable from VernisFS and create a Ring 3 task.
static int elf_exec_32(const char *path, uint16_t ticks) {
    int file_size = kfs_read_file(path, elf_load_buf_32, ELF_BUF_SIZE);
    if (file_size < (int)sizeof(Elf32_Ehdr)) {
        serial_print("[elf] failed to read: ");
        serial_print(path);
        serial_print("\n");
        return -1;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_load_buf_32;
    if (ehdr->e_magic != ELF_MAGIC || ehdr->e_class != 1 ||
        ehdr->e_data != 1 || ehdr->e_machine != EM_386 ||
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

    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++)
        if (!task_slots[i].active) { slot = i; break; }
    if (slot < 0) {
        serial_print("[elf] no free task slot\n");
        return -1;
    }

    uint32_t pid = scheduler_create_process(kernel_scheduler, 5, path);

    task_slots[slot].active          = 1;
    task_slots[slot].pid             = pid;
    task_slots[slot].ticks_remaining = ticks;
    task_slots[slot].ticks_total     = ticks;
    task_slots[slot].ppid_slot       = 0;
    task_slots[slot].brk             = 0;
    fd_table_init_32(slot);
    vma_init_32(task_slots[slot].vma_list);    // Phase 46: init VMAs
    task_slots[slot].mmap_next       = 0;
    Elf32_Phdr *phdr = (Elf32_Phdr *)(elf_load_buf_32 + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_memsz == 0) continue;

        uint32_t seg_vaddr  = phdr[i].p_vaddr;
        uint32_t seg_filesz = phdr[i].p_filesz;
        uint32_t seg_memsz  = phdr[i].p_memsz;
        uint32_t seg_foff   = phdr[i].p_offset;

        uint32_t vaddr = seg_vaddr & ~(PAGE_SIZE - 1U);
        uint32_t vend  = (seg_vaddr + seg_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1U);

        for (uint32_t va = vaddr; va < vend; va += PAGE_SIZE) {
            uint32_t frame = frame_alloc_32();
            if (!frame) {
                serial_print("[elf] out of frames\n");
                task_slots[slot].active = 0;
                return -1;
            }

            for (int b = 0; b < PAGE_SIZE; b++) {
                uint32_t abs_va = va + (uint32_t)b;
                if (abs_va < seg_vaddr || abs_va >= seg_vaddr + seg_memsz)
                    continue;
                uint32_t seg_off = abs_va - seg_vaddr;
                if (seg_off < seg_filesz) {
                    uint32_t foff = seg_foff + seg_off;
                    if (foff < (uint32_t)file_size)
                        ((uint8_t *)frame)[b] = elf_load_buf_32[foff];
                }
            }

            paging_map_4k_32(kernel_page_dir, va, frame, PAGE_USER_32);
        }
    }

    // Map user stack (16 KB = 4 pages)
    for (uint32_t i = 0; i < USER_STACK_SIZE_32; i += PAGE_SIZE) {
        uint32_t frame = frame_alloc_32();
        if (!frame) {
            serial_print("[elf] out of frames for stack\n");
            task_slots[slot].active = 0;
            return -1;
        }
        paging_map_4k_32(kernel_page_dir, USER_STACK_TOP_32 - USER_STACK_SIZE_32 + i,
                         frame, PAGE_USER_32);
    }
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");

    // Build iret frame on kernel stack
    uint32_t *sp = (uint32_t*)(task_slots[slot].stack + TASK_STACK_SIZE);

    *(--sp) = 0x20 | 3;                         // SS  (0x23)
    *(--sp) = USER_STACK_TOP_32;                 // ESP
    *(--sp) = 0x202;                             // EFLAGS (IF=1)
    *(--sp) = 0x18 | 3;                          // CS  (0x1B)
    *(--sp) = ehdr->e_entry;                     // EIP = ELF entry point

    *(--sp) = 0;   // error_code
    *(--sp) = 0;   // int_no

    *(--sp) = 0;   // EAX
    *(--sp) = 0;   // ECX
    *(--sp) = 0;   // EDX
    *(--sp) = 0;   // EBX
    *(--sp) = 0;   // ESP (ignored by popa)
    *(--sp) = 0;   // EBP
    *(--sp) = 0;   // ESI
    *(--sp) = 0;   // EDI

    *(--sp) = 0x23;  // DS
    *(--sp) = 0x23;  // ES
    *(--sp) = 0x23;  // FS
    *(--sp) = 0x23;  // GS

    task_slots[slot].esp = (uint32_t)sp;

    serial_print("[elf] loaded ");
    serial_print(path);
    serial_print(" entry=0x");
    {
        uint32_t v = ehdr->e_entry;
        char hx[9]; hx[8] = 0;
        for (int d = 7; d >= 0; d--) {
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

static int kernel_elf_exec_32(const char *path) {
    int slot = elf_exec_32(path, 24);
    if (slot >= 0) {
        kernel_tss32.esp0 = (uint32_t)(task_slots[slot].stack + TASK_STACK_SIZE);
    }
    return slot >= 0 ? 0 : -1;
}

int (*g_elf_exec_fn)(const char *path) = (void *)0;

// =============================================================================
// Phase 43: fork / execve / sbrk implementations (x86)
// Placed after ELF types so Elf32_Ehdr / elf_load_buf_32 are visible.
// =============================================================================

static int32_t sys_fork_32(InterruptFrame32 *frame) {
    if (current_task_idx < 0) return -1;
    int child_slot = -1;
    for (int i = 0; i < MAX_TASKS; i++)
        if (!task_slots[i].active) { child_slot = i; break; }
    if (child_slot < 0) return -1;

    uint32_t child_pid = scheduler_create_process(kernel_scheduler, 5, "fork-child");

    // Copy parent kernel stack
    for (int i = 0; i < TASK_STACK_SIZE; i++)
        task_slots[child_slot].stack[i] = task_slots[current_task_idx].stack[i];

    // Copy fd table
    fd_copy_32(current_task_idx, child_slot);

    task_slots[child_slot].active = 1;
    task_slots[child_slot].pid = child_pid;
    task_slots[child_slot].ticks_remaining = task_slots[current_task_idx].ticks_total;
    task_slots[child_slot].ticks_total = task_slots[current_task_idx].ticks_total;
    task_slots[child_slot].ppid_slot = (uint32_t)current_task_idx;
    task_slots[child_slot].brk = task_slots[current_task_idx].brk;

    // Copy VMA list (Phase 46)
    for (int i = 0; i < VMA_MAX_PER_TASK; i++)
        task_slots[child_slot].vma_list[i] = task_slots[current_task_idx].vma_list[i];
    task_slots[child_slot].mmap_next = task_slots[current_task_idx].mmap_next;

    // Compute child ESP from parent frame offset
    uint32_t parent_stack_base = (uint32_t)task_slots[current_task_idx].stack;
    uint32_t child_stack_base  = (uint32_t)task_slots[child_slot].stack;
    uint32_t frame_off = (uint32_t)frame - parent_stack_base;
    InterruptFrame32 *child_frame = (InterruptFrame32 *)(child_stack_base + frame_off);
    child_frame->eax = 0;   // child returns 0 from fork
    task_slots[child_slot].esp = (uint32_t)child_frame;

    serial_print("[fork] parent_slot=");
    serial_print_dec(current_task_idx);
    serial_print(" child_slot=");
    serial_print_dec(child_slot);
    serial_print(" child_pid=");
    serial_print_dec(child_pid);
    serial_print("\n");

    return (int32_t)child_pid;
}

static int32_t sys_execve_32(InterruptFrame32 *frame, uint32_t path_ptr) {
    if (current_task_idx < 0) return -1;
    char path[64];
    if (copy_user_path_32(path, path_ptr) < 0) return -1;

    int file_size = kfs_read_file(path, elf_load_buf_32, ELF_BUF_SIZE);
    if (file_size < (int)sizeof(Elf32_Ehdr)) return -1;

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_load_buf_32;
    if (ehdr->e_magic != ELF_MAGIC || ehdr->e_class != 1 ||
        ehdr->e_data != 1 || ehdr->e_machine != EM_386 ||
        ehdr->e_type != ET_EXEC)
        return -1;

    // Map PT_LOAD segments (replaces current process image)
    Elf32_Phdr *phdr = (Elf32_Phdr *)(elf_load_buf_32 + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) continue;
        uint32_t seg_vaddr  = phdr[i].p_vaddr;
        uint32_t seg_filesz = phdr[i].p_filesz;
        uint32_t seg_memsz  = phdr[i].p_memsz;
        uint32_t seg_foff   = phdr[i].p_offset;
        uint32_t vaddr = seg_vaddr & ~(PAGE_SIZE - 1U);
        uint32_t vend  = (seg_vaddr + seg_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1U);
        for (uint32_t va = vaddr; va < vend; va += PAGE_SIZE) {
            uint32_t f = frame_alloc_32();
            if (!f) return -1;
            for (int b = 0; b < PAGE_SIZE; b++) {
                uint32_t abs_va = va + (uint32_t)b;
                if (abs_va < seg_vaddr || abs_va >= seg_vaddr + seg_memsz) continue;
                uint32_t seg_off = abs_va - seg_vaddr;
                if (seg_off < seg_filesz) {
                    uint32_t foff = seg_foff + seg_off;
                    if (foff < (uint32_t)file_size)
                        ((uint8_t *)f)[b] = elf_load_buf_32[foff];
                }
            }
            paging_map_4k_32(kernel_page_dir, va, f, PAGE_USER_32);
        }
    }

    // Remap user stack
    for (uint32_t i = 0; i < USER_STACK_SIZE_32; i += PAGE_SIZE) {
        uint32_t f = frame_alloc_32();
        if (!f) return -1;
        paging_map_4k_32(kernel_page_dir, USER_STACK_TOP_32 - USER_STACK_SIZE_32 + i,
                         f, PAGE_USER_32);
    }
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");

    // Reset fd table (keep 0/1/2)
    for (int i = 3; i < FD_MAX; i++)
        fd_close_entry_32(&task_slots[current_task_idx].fd_table[i]);
    task_slots[current_task_idx].brk = 0;

    // Modify the interrupt frame to jump to new entry
    frame->eip    = ehdr->e_entry;
    frame->cs     = 0x18 | 3;
    frame->eflags = 0x202;
    frame->user_esp = USER_STACK_TOP_32;
    frame->user_ss  = 0x20 | 3;
    frame->eax = frame->ebx = frame->ecx = frame->edx = 0;
    frame->esi = frame->edi = frame->ebp = 0;
    frame->ds = frame->es = frame->fs = frame->gs = 0x20 | 3;

    serial_print("[execve] loaded ");
    serial_print(path);
    serial_print(" entry=0x");
    serial_print_hex(ehdr->e_entry);
    serial_print("\n");
    return 0;
}

static int32_t sys_sbrk_32(int32_t incr) {
    if (current_task_idx < 0) return -1;
    TaskSlot32 *t = &task_slots[current_task_idx];
    if (t->brk == 0)
        t->brk = 0x20000000;  // initial program break
    uint32_t old_brk = t->brk;
    if (incr == 0) return (int32_t)old_brk;
    uint32_t new_brk = old_brk + (uint32_t)incr;
    if (incr > 0) {
        uint32_t page_start = (old_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint32_t page_end   = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (uint32_t va = page_start; va < page_end; va += PAGE_SIZE) {
            uint32_t f = frame_alloc_32();
            if (!f) return -1;
            paging_map_4k_32(kernel_page_dir, va, f, PAGE_USER_32);
        }
    }
    t->brk = new_brk;
    return (int32_t)old_brk;
}

// =============================================================================
// Phase 46: VMA helpers + mmap / munmap syscalls (x86)
// =============================================================================

static VmaEntry32 *vma_find_32(VmaEntry32 *vmas, uint32_t addr) {
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (vmas[i].type != VMA_TYPE_NONE) {
            if (addr >= vmas[i].start && addr < vmas[i].start + vmas[i].length)
                return &vmas[i];
        }
    }
    return (VmaEntry32 *)0;
}

static void vma_init_32(VmaEntry32 *vmas) {
    for (int i = 0; i < VMA_MAX_PER_TASK; i++)
        vmas[i].type = VMA_TYPE_NONE;
}

// mmap_file_tmp_32 defined earlier (forward declaration section)

static int32_t sys_mmap_32(uint32_t length, uint32_t prot_flags, uint32_t path_ptr) {
    if (current_task_idx < 0) return -1;
    TaskSlot32 *t = &task_slots[current_task_idx];

    length = (length + 0xFFF) & ~0xFFFU;
    if (length == 0 || length > 0x1000000U) return -1;

    uint8_t prot  = (uint8_t)(prot_flags & 0xFF);
    uint8_t flags = (uint8_t)((prot_flags >> 8) & 0xFF);

    int slot = -1;
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (t->vma_list[i].type == VMA_TYPE_NONE) { slot = i; break; }
    }
    if (slot < 0) return -1;

    if (t->mmap_next == 0) t->mmap_next = MMAP_BASE_32;
    uint32_t va = t->mmap_next;
    t->mmap_next += length;

    VmaEntry32 *v = &t->vma_list[slot];
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
        if (copy_user_path_32(v->path, path_ptr) < 0) {
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

    return (int32_t)va;
}

static int32_t sys_munmap_32(uint32_t addr, uint32_t length) {
    if (current_task_idx < 0) return -1;
    TaskSlot32 *t = &task_slots[current_task_idx];
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
    // MEM_PRESSURE / THROTTLE — logged via serial only
}

// Phase 10: AI Remediation handler
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

    uint32_t pid = 0;
    if (target[0] != 'a') {
        for (const char *d = target; *d >= '0' && *d <= '9'; d++)
            pid = pid * 10 + (uint32_t)(*d - '0');
    }

    if (action[0] == 't' && action[1] == 'h') {
        uint32_t quantum = 0;
        if (param) {
            for (const char *d = param; *d >= '0' && *d <= '9'; d++)
                quantum = quantum * 10 + (uint32_t)(*d - '0');
        }
        if (quantum < 1) quantum = 10;
        if (quantum > 200) quantum = 200;
        scheduler_set_quantum(kernel_scheduler, quantum);
        return;
    }

    if (action[0] == 'k' && action[1] == 'i') {
        if (pid > 0) {
            scheduler_set_priority(kernel_scheduler, pid, 0);
        }
        return;
    }
}

// =============================================================================
// Deferred AI engine work — called from CLI idle loop (main thread context)
// Safe for heap allocations; timer IRQ only sets kernel_tick.
// =============================================================================

static uint32_t g_ai_last_tick  = 0;
static uint32_t g_ai_last_stat  = 0;

void kernel_idle_work(void) {
    uint32_t now = kernel_tick;
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
// =============================================================================
// GUI ↔ CLI bridge
// =============================================================================

static CliShell *gui_shell = 0;
static CliSession *gui_session = 0;

void cli_process_line_gui(const uint8_t *line, uint32_t len) {
    if (!gui_shell || !gui_session || !line || len == 0) return;
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
    *(volatile uint32_t *)(pb + 0x04) = 0;
    *(volatile uint32_t *)(pb + 0x08) = (uint32_t)fb;
    *(volatile uint32_t *)(pb + 0x0C) = 0;

    ahci_memzero(&ahci_cmd_headers[port][0], sizeof(ahci_cmd_headers[port]));
    ahci_memzero(&ahci_rfis[port][0], sizeof(ahci_rfis[port]));

    for (int i = 0; i < AHCI_MAX_SLOTS; i++) {
        AhciCmdHeader *h = &ahci_cmd_headers[port][i];
        uintptr_t ctba = (uintptr_t)&ahci_cmd_tables[port][i];
        h->ctba = (uint32_t)ctba;
        h->ctbau = 0;
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
    t->prdt[0].dbau = 0;
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
    t->prdt[0].dbau = 0;
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
    t->prdt[0].dbau = 0;
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

    uint32_t abar_phys = bar5 & ~0xFu;
    if (abar_phys == 0) return -1;

    for (uint32_t off = 0; off < 0x2000; off += 0x1000) {
        paging_map_4k_32(kernel_page_dir, abar_phys + off, abar_phys + off,
                         PAGE_PRESENT_32 | PAGE_WRITABLE_32);
    }
    paging_flush_tlb_32();

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
static uint32_t nvme_mqes = 0;
static int nvme_up = 0;

#define NVME_AQ_DEPTH 16

typedef struct {
    uint32_t cdw0;
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

typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) NvmeCqe;

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

static uint32_t nvme_db_stride = 4;

static void nvme_ring_asq(void) {
    nvme_write32(0x1000, nvme_asq_tail);
}

static int nvme_wait_acq(uint16_t cid, uint32_t *result) {
    for (int spin = 0; spin < 4000000; spin++) {
        volatile NvmeCqe *cqe = &nvme_acq[nvme_acq_head];
        uint8_t p = (uint8_t)(cqe->status & 1u);
        if (p == nvme_acq_phase) {
            uint16_t got_cid = cqe->cid;
            uint16_t sc = (cqe->status >> 1) & 0x7FFu;
            if (result) *result = cqe->dw0;

            nvme_acq_head++;
            if (nvme_acq_head >= NVME_AQ_DEPTH) {
                nvme_acq_head = 0;
                nvme_acq_phase = (uint8_t)(nvme_acq_phase ^ 1u);
            }
            nvme_write32(0x1000 + nvme_db_stride, nvme_acq_head);

            if (got_cid != cid) continue;
            return (sc == 0) ? 0 : -(int)sc;
        }
    }
    return -999;
}

static int nvme_identify_controller(void) {
    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_asq_tail;

    nvme_memzero(&nvme_asq[slot], sizeof(NvmeSqe));
    nvme_asq[slot].cdw0 = 0x06u | ((uint32_t)cid << 16);
    nvme_asq[slot].nsid = 0;
    nvme_asq[slot].prp1 = (uint64_t)(uintptr_t)&nvme_ident_buf[0];
    nvme_asq[slot].prp2 = 0;
    nvme_asq[slot].cdw10 = 1;

    nvme_asq_tail++;
    if (nvme_asq_tail >= NVME_AQ_DEPTH) nvme_asq_tail = 0;
    nvme_ring_asq();

    return nvme_wait_acq(cid, 0);
}

static void nvme_extract_str(const uint8_t *src, int len, char *dst, int dmax) {
    int i;
    int copy = len < dmax - 1 ? len : dmax - 1;
    for (i = 0; i < copy; i++) dst[i] = (char)src[i];
    dst[i] = '\0';
    while (i > 0 && dst[i - 1] == ' ') { i--; dst[i] = '\0'; }
}

static int nvme_init(PciDev *dev) {
    uint32_t cmd = pci_read32(dev->bus, dev->slot, 0, 0x04);
    cmd |= (1u << 2) | (1u << 1);
    pci_write32(dev->bus, dev->slot, 0, 0x04, cmd);

    uint32_t bar0 = pci_read32(dev->bus, dev->slot, 0, 0x10);
    if (bar0 & 1u) return -1;

    uint32_t bar0_phys = bar0 & ~0xFu;
    if (bar0_phys == 0) return -1;

    for (uint32_t off = 0; off < 0x4000; off += 4096) {
        paging_map_4k_32(kernel_page_dir, bar0_phys + off, bar0_phys + off,
                         PAGE_PRESENT_32 | PAGE_WRITABLE_32);
    }
    paging_flush_tlb_32();

    nvme_mmio = (volatile uint8_t *)(uintptr_t)bar0_phys;

    nvme_cap = nvme_read64(0x00);
    nvme_vs  = nvme_read32(0x08);
    nvme_mqes = (uint32_t)(nvme_cap & 0xFFFFu);
    uint32_t dstrd = (uint32_t)((nvme_cap >> 32) & 0xFu);
    nvme_db_stride = 4u << dstrd;

    uint32_t cc = nvme_read32(0x14);
    if (cc & 1u) {
        nvme_write32(0x14, cc & ~1u);
        for (int spin = 0; spin < 2000000; spin++) {
            uint32_t csts = nvme_read32(0x1C);
            if (!(csts & 1u)) break;
        }
    }

    nvme_memzero(nvme_asq, sizeof(nvme_asq));
    nvme_memzero(nvme_acq, sizeof(nvme_acq));
    nvme_asq_tail = 0;
    nvme_acq_head = 0;
    nvme_acq_phase = 1;
    nvme_cid_counter = 0;

    uint32_t aqa = ((uint32_t)(NVME_AQ_DEPTH - 1) << 16) | (uint32_t)(NVME_AQ_DEPTH - 1);
    nvme_write32(0x24, aqa);

    nvme_write64(0x28, (uint64_t)(uintptr_t)nvme_asq);
    nvme_write64(0x30, (uint64_t)(uintptr_t)nvme_acq);

    cc = (6u << 16) | (4u << 20) | 1u;
    nvme_write32(0x14, cc);

    for (int spin = 0; spin < 4000000; spin++) {
        uint32_t csts = nvme_read32(0x1C);
        if (csts & 1u) {
            nvme_up = 1;
            serial_print("[nvme] controller ready\n");

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
        if (csts & (1u << 1)) return -2;
    }
    return -3;
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

static int nvme_create_iocq(void) {
    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_asq_tail;

    nvme_memzero(&nvme_asq[slot], sizeof(NvmeSqe));
    nvme_asq[slot].cdw0 = 0x05u | ((uint32_t)cid << 16);
    nvme_asq[slot].prp1 = (uint64_t)(uintptr_t)nvme_iocq;
    nvme_asq[slot].cdw10 = ((uint32_t)(NVME_IOQ_DEPTH - 1) << 16) | 1u;
    nvme_asq[slot].cdw11 = 1u;

    nvme_asq_tail++;
    if (nvme_asq_tail >= NVME_AQ_DEPTH) nvme_asq_tail = 0;
    nvme_ring_asq();

    return nvme_wait_acq(cid, 0);
}

static int nvme_create_iosq(void) {
    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_asq_tail;

    nvme_memzero(&nvme_asq[slot], sizeof(NvmeSqe));
    nvme_asq[slot].cdw0 = 0x01u | ((uint32_t)cid << 16);
    nvme_asq[slot].prp1 = (uint64_t)(uintptr_t)nvme_iosq;
    nvme_asq[slot].cdw10 = ((uint32_t)(NVME_IOQ_DEPTH - 1) << 16) | 1u;
    nvme_asq[slot].cdw11 = (1u << 16) | 1u;

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

static void nvme_ring_iosq(void) {
    nvme_write32(0x1000 + 2u * nvme_db_stride, nvme_iosq_tail);
}

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
            nvme_write32(0x1000 + 3u * nvme_db_stride, nvme_iocq_head);

            if (got_cid != cid) continue;
            return (sc == 0) ? 0 : -(int)sc;
        }
    }
    return -999;
}

static void nvme_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

int kernel_nvme_read(uint64_t lba, uint32_t sectors, uint8_t *out, uint32_t out_max) {
    if (!nvme_up || !nvme_ioq_up) return -1;
    if (sectors == 0 || sectors > 8) return -9;
    uint32_t bytes = sectors * 512u;
    if (!out || out_max < bytes) return -10;

    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_iosq_tail;

    nvme_memzero(&nvme_iosq[slot], sizeof(NvmeSqe));
    nvme_iosq[slot].cdw0 = 0x02u | ((uint32_t)cid << 16);
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

    nvme_memcpy(out, nvme_io_buf, bytes);
    return (int)bytes;
}

int kernel_nvme_write(uint64_t lba, uint32_t sectors, const uint8_t *data, uint32_t data_len) {
    if (!nvme_up || !nvme_ioq_up) return -1;
    if (sectors == 0 || sectors > 8) return -9;
    uint32_t bytes = sectors * 512u;
    if (!data || data_len < bytes) return -10;

    nvme_memcpy(nvme_io_buf, data, bytes);

    uint16_t cid = nvme_cid_counter++;
    int slot = nvme_iosq_tail;

    nvme_memzero(&nvme_iosq[slot], sizeof(NvmeSqe));
    nvme_iosq[slot].cdw0 = 0x01u | ((uint32_t)cid << 16);
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
// Phase 22: E1000 NIC Driver (Intel 82540EM) — x86
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
    uint32_t cmd = pci_read32(dev->bus, dev->slot, 0, 0x04);
    cmd |= (1u << 2) | (1u << 1);
    pci_write32(dev->bus, dev->slot, 0, 0x04, cmd);

    uint32_t mmio_phys = dev->bar0 & ~0xFu;

    for (uint32_t off = 0; off < 0x20000; off += 0x1000) {
        paging_map_4k_32(kernel_page_dir, mmio_phys + off, mmio_phys + off,
                         PAGE_PRESENT_32 | PAGE_WRITABLE_32);
    }
    paging_flush_tlb_32();
    e1000_mmio = (volatile uint8_t *)(uintptr_t)mmio_phys;

    e1000_w(E1000_CTRL, e1000_r(E1000_CTRL) | E1000_CTRL_RST);
    for (volatile int i = 0; i < 1000000; i++) {}

    e1000_w(E1000_IMC, 0xFFFFFFFF);
    (void)e1000_r(E1000_ICR);

    uint32_t ctrl = e1000_r(E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~(E1000_CTRL_RST | (1u << 3) | (1u << 31) | (1u << 30));
    e1000_w(E1000_CTRL, ctrl);

    for (volatile int i = 0; i < 1000000; i++) {}

    uint32_t ral = e1000_r(E1000_RAL);
    uint32_t rah = e1000_r(E1000_RAH);
    e1000_mac[0] = ral; e1000_mac[1] = ral >> 8;
    e1000_mac[2] = ral >> 16; e1000_mac[3] = ral >> 24;
    e1000_mac[4] = rah; e1000_mac[5] = rah >> 8;
    e1000_w(E1000_RAH, (rah & 0xFFFF) | (1u << 31));

    for (int i = 0; i < 128; i++) e1000_w(E1000_MTA + i * 4, 0);

    for (int i = 0; i < E1000_NUM_RX; i++) {
        e1000_rx_ring[i].addr = (uint64_t)(uint32_t)(uintptr_t)e1000_rx_bufs[i];
        e1000_rx_ring[i].status = 0;
    }
    e1000_w(E1000_RDBAL, (uint32_t)(uintptr_t)e1000_rx_ring);
    e1000_w(E1000_RDBAH, 0);
    e1000_w(E1000_RDLEN, E1000_NUM_RX * sizeof(E1000RxDesc));
    e1000_w(E1000_RDH, 0);
    e1000_w(E1000_RDT, E1000_NUM_RX - 1);
    e1000_w(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    e1000_rx_cur = 0;

    for (int i = 0; i < E1000_NUM_TX; i++) {
        e1000_tx_ring[i].addr = (uint64_t)(uint32_t)(uintptr_t)e1000_tx_bufs[i];
        e1000_tx_ring[i].status = 1;
        e1000_tx_ring[i].cmd = 0;
    }
    e1000_w(E1000_TDBAL, (uint32_t)(uintptr_t)e1000_tx_ring);
    e1000_w(E1000_TDBAH, 0);
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
// Phase 22: Minimal Network Stack (ARP + ICMP) — x86
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
    uint32_t start = kernel_tick;
    uint32_t last_arp = start;
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
    uint8_t pkt[14 + 20 + 8 + 32];
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

    uint8_t buf[E1000_PKT_SIZE];
    uint32_t start = kernel_tick;
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

void _start(void) __attribute__((section(".text.entry"), naked));
void kernel_main(void);

void _start(void) {
    __asm__ volatile(
        // Stack at 15 MB — above BSS end (~9.3 MB with 8 MB heap)
        "mov $0xF00000, %%esp\n\t"
        "call kernel_main\n\t"
        "cli\n\t"
        "1: hlt\n\t"
        "jmp 1b\n\t"
        ::: "memory"
    );
}

void kernel_main(void) {
    // Zero BSS — flat binary doesn't zero SHT_NOBITS sections
    // Rust ALLOCATOR (LockedHeap::empty) must be zero-initialized
    {
        extern char _bss_start[], _bss_end[];
        volatile char *p = _bss_start;
        while (p < (volatile char *)_bss_end) *p++ = 0;
    }

    // Serial
    serial_init();
    serial_print("\n[VernisOS x86] kernel_main() entered\n");

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

    // VGA
    terminal_initialize();
    clear_screen();

    // Heap (must be first Rust call)
    serial_print("[x86] heap init addr=");
    serial_print_hex((uint32_t)kernel_heap);
    serial_print("\n");
    verniskernel_init_heap((uint32_t)kernel_heap, HEAP_SIZE);
    serial_print("[x86] heap initialized, 8MB\n");

    // ----- Phase 16: Enable paging (identity map + PSE 4MB pages) -----
    paging_init_32();

    // ----- Framebuffer init (after heap) -----
    if (display_mode == 1) {
        volatile struct boot_info *bi = (volatile struct boot_info *)0x5300;
        fb_init(bi->fb_addr, bi->fb_width, bi->fb_height, bi->fb_pitch, bi->fb_bpp);
        console_init(bi->fb_width, bi->fb_height);
        console_clear();
        serial_print("[fb] Framebuffer mode: ");
        serial_print_dec(bi->fb_width);
        serial_print("x");
        serial_print_dec(bi->fb_height);
        serial_print("x");
        serial_print_dec(bi->fb_bpp);
        serial_print(" @ ");
        serial_print_hex(bi->fb_addr);
        serial_print("\n");
    }

    terminal_setcolor(make_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("VernisOS x86 Kernel\n");
    terminal_writestring("===================\n");

    // Register serial callback → enables Rust modules to write to serial
    verniskernel_register_print(rust_print_cb);
    serial_print("[x86] Rust print callback registered\n");

    // GDT
    gdt_init();
    serial_print("[x86] GDT loaded\n");

    // IDT
    idt_init();
    serial_print("[x86] IDT loaded (256 gates)\n");

    // PIC
    pic_init();
    serial_print("[x86] PIC remapped: IRQ0-7->0x20, IRQ8-15->0x28\n");

    // PIT
    pit_init();
    serial_print("[x86] PIT set to 240Hz\n");

    // Keyboard
    keyboard_init();
    tty_init_32(&kernel_tty0_32);
    serial_print("[x86] keyboard + TTY initialized\n");

    // Mouse (when framebuffer is available)
    if (display_mode >= 1) {
        ps2_mouse_init();
        volatile struct boot_info *bi = (volatile struct boot_info *)0x5300;
        mouse_init(bi->fb_width, bi->fb_height);
    }

    // =========================================================
    // Phase 3: Core Kernel — syscall + memory + scheduling
    // =========================================================

    // Syscall subsystem
    syscall_init();
    serial_print("[x86] syscall subsystem initialized\n");

    // IPC subsystem (Phase 4)
    ipc_init();

    // =========================================================
    // Phase 5: Module Loader
    // =========================================================
    module_init();
    serial_print("[x86] module loader initialized\n");

    // =========================================================
    // Phase 6: User Sandbox Environment
    // =========================================================
    sandbox_init();
    serial_print("[x86] sandbox environment initialized\n");

    // =========================================================
    // Phase 8: AI IPC Bridge
    // =========================================================
    ai_bridge_init();
    ai_send_event(AI_EVT_BOOT, "vernisOS x86 kernel started");
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
    ai_kernel_engine_feed(AI_EVT_BOOT, "vernisOS x86 kernel started", kernel_tick);


    // =========================================================
    // Phase 7: CLI / Terminal System
    // =========================================================
    CliShell *shell = cli_shell_init();
    CliSession *user_session = cli_session_create(shell, "root", CLI_PRIV_ROOT);
    gui_shell = shell;
    gui_session = user_session;
    serial_print("[x86] CLI system initialized\n");

    // Scheduler — create and register init process (PID 1)
    kernel_scheduler = scheduler_new();
    uint32_t init_pid = scheduler_create_process(kernel_scheduler, 10, "init");
    uint32_t ai_pid   = scheduler_create_process(kernel_scheduler, 8,  "ai_engine");
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
    scheduler_schedule(kernel_scheduler);
    serial_print("[x86] scheduler initialized, init PID=");
    serial_print_dec(init_pid);
    serial_print(", processes=");
    serial_print_dec(scheduler_get_process_count(kernel_scheduler));
    serial_print("\n");

    // ----- Phase 18: Preemptive Context Switch -----
    serial_print("[phase18] context switch setup...\n");
    task_register_main_32(init_pid, 24);   // 100ms quantum at 240 Hz
    {
        uint32_t worker_pid = scheduler_create_process(kernel_scheduler, 9, "worker");
        int worker_idx = task_create_32(phase18_worker_entry_32, worker_pid, 24);
        if (worker_idx >= 0) {
            serial_print("[phase18] worker task created (pid=");
            serial_print_dec(worker_pid);
            serial_print(")\n");
        }
    }
    context_switch_enabled = 1;
    serial_print("[phase18] preemptive multitasking enabled\n");

    // ----- Phase 45: User shell boot -----
    g_elf_exec_fn = kernel_elf_exec_32;
    {
        const void *vsh = vfs_find_file("/bin/vsh32");
        if (vsh) {
            serial_print("[phase45] found /bin/vsh32, launching user shell...\n");
            int elf_slot = elf_exec_32("/bin/vsh32", 24);
            if (elf_slot >= 0) {
                serial_print("[phase45] user shell task running\n");
            } else {
                serial_print("[phase45] /bin/vsh32 load failed\n");
            }
        } else {
            serial_print("[phase45] /bin/vsh32 not found — shell-only boot mode\n");
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

    // Enable interrupts (after all subsystems are ready)
    __asm__ volatile("sti");
    serial_print("[x86] interrupts enabled\n");

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
        gui_init(bi->fb_width, bi->fb_height);
        display_mode = 2;
        serial_print("[gui] GUI initialized, entering main loop\n");

        while (1) {
            gui_main_loop_tick();
            __asm__ volatile("hlt");
        }
    }

    // Run interactive shell
    if (user_session) {
        cli_shell_loop(user_session);
    }

    // If shell exits, just halt
    serial_print("[x86] shell exited\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
