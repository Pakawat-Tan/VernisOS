#include <stdint.h>
#include <stddef.h>

// VGA text mode color constants
#define VGA_COLOR_BLACK        0
#define VGA_COLOR_BLUE         1
#define VGA_COLOR_GREEN        2
#define VGA_COLOR_CYAN         3
#define VGA_COLOR_RED          4
#define VGA_COLOR_MAGENTA      5
#define VGA_COLOR_BROWN        6
#define VGA_COLOR_LIGHT_GREY   7
#define VGA_COLOR_DARK_GREY    8
#define VGA_COLOR_LIGHT_BLUE   9
#define VGA_COLOR_LIGHT_GREEN  10
#define VGA_COLOR_LIGHT_CYAN   11
#define VGA_COLOR_LIGHT_RED    12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_LIGHT_BROWN  14
#define VGA_COLOR_WHITE        15
#define VGA_COLOR_YELLOW       VGA_COLOR_LIGHT_BROWN  // Yellow is same as light brown in VGA

// Hardware text mode color constants
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

// Create a color attribute byte
static inline uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

// Create a VGA entry
static inline uint16_t make_vgaentry(char c, uint8_t color) {
    uint16_t c16 = c;
    uint16_t color16 = color;
    return c16 | (color16 << 8);
}

// Write directly to VGA memory for immediate debug
static void write_debug_immediate(const char* str, int line) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    uint8_t color = make_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    size_t offset = line * 80;  // line offset
    size_t i = 0;
    while (str[i] != '\0' && i < 79) {  // leave space for null
        vga[offset + i] = make_vgaentry(str[i], color);
        i++;
    }
}

// Initialize terminal without clearing screen initially
static void terminal_initialize() {
    terminal_row = 2;  // เริ่มที่บรรทัดที่ 3 เพื่อไม่ทับข้อความ debug
    terminal_column = 0;
    terminal_color = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;
}

// Set terminal color
static void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

// Put entry at specific position
static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = make_vgaentry(c, color);
}

// Put a character with proper bounds checking
static void terminal_putchar(char c) {
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row >= VGA_HEIGHT) {
            // Scroll up
            for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    terminal_buffer[y * VGA_WIDTH + x] = terminal_buffer[(y + 1) * VGA_WIDTH + x];
                }
            }
            // Clear last line
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
            }
            terminal_row = VGA_HEIGHT - 1;
        }
        return;
    }

    if (terminal_row < VGA_HEIGHT && terminal_column < VGA_WIDTH) {
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    }
    
    if (++terminal_column >= VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row >= VGA_HEIGHT) {
            // Scroll up
            for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    terminal_buffer[y * VGA_WIDTH + x] = terminal_buffer[(y + 1) * VGA_WIDTH + x];
                }
            }
            // Clear last line
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
            }
            terminal_row = VGA_HEIGHT - 1;
        }
    }
}

// Write string
static void terminal_writestring(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        terminal_putchar(data[i]);
    }
}

// Simple delay function
static void delay(uint64_t count) {
    volatile uint64_t i;
    for (i = 0; i < count; i++) {
        __asm__ volatile("nop");
    }
}

// Clear the screen
void clear_screen() {
    uint16_t blank = make_vgaentry(' ', make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        terminal_buffer[i] = blank;
    }
}

// Entry point for the 64-bit kernel - ต้องเป็น first function
void kernel_main(void) __attribute__((section(".text.entry")));

void kernel_main(void) {
    // Initialize terminal
    terminal_initialize();
    clear_screen();
    
    // Set color to light green on black
    terminal_setcolor(make_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    
    // Write only the required messages
    terminal_writestring("Kernel x86-64 is running!\n");
    terminal_writestring("VernisOS loaded successfully\n");
    
    // Halt the CPU
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}