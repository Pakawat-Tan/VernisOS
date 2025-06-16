#define VIDEO_MEMORY ((volatile char*)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

// Function to initialize segments
static inline void init_segments() {
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : : "ax"
    );
}

// Write a character with color attribute to video memory
void write_char(int x, int y, char c, char attr) {
    int offset = (y * VGA_WIDTH + x) * 2;
    VIDEO_MEMORY[offset] = c;
    VIDEO_MEMORY[offset + 1] = attr;
}

// Write a string to video memory
void write_string(int x, int y, const char* str, char attr) {
    int i = 0;
    while (str[i] != '\0') {
        write_char(x + i, y, str[i], attr);
        i++;
    }
}

// Clear the screen
void clear_screen() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            write_char(x, y, ' ', 0x07);
        }
    }
}

// Forward declaration
void kernel_main(void);

// Kernel entry point
__attribute__((section(".text.entry")))
__attribute__((naked))
void _start(void) {
    __asm__ volatile(
        // Set up stack
        "mov $0x90000, %%esp\n"
        "mov %%esp, %%ebp\n"
        
        // Set up segments
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        
        // Write directly to video memory to show we're alive
        "mov $0xB8000, %%edi\n"
        "movl $0x4F4B4F4F, (%%edi)\n"  // "OK" in white on red
        
        // Jump to C code
        "call kernel_main\n"
        
        // Halt if we return
        "cli\n"
        "1: hlt\n"
        "jmp 1b\n"
        : : : "eax", "edi"
    );
}

// Main kernel function
void kernel_main(void) {
    // Clear screen
    clear_screen();
    
    // Write some text to show we're running
    write_string(0, 0, "Kernel x86 is running!", 0x0F);    // White on black
    write_string(0, 1, "VernisOS loaded successfully", 0x0A);  // Green on black
    
    // Halt the CPU
    while(1) {
        __asm__ volatile("hlt");
    }
} 