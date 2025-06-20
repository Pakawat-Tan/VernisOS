#include "kernel.h"

// Memory-mapped I/O functions
static inline void mmio_write(uint64_t reg, uint32_t data) {
    *(volatile uint32_t*)reg = data;
}

static inline uint32_t mmio_read(uint64_t reg) {
    return *(volatile uint32_t*)reg;
}

// Initialize UART
void uart_init(void) {
    // Disable UART
    mmio_write(UART_CR, 0x00000000);
    
    // Set baud rate to 115200
    // UART clock = 24MHz, baud rate = 115200
    // Divisor = 24000000 / (16 * 115200) = 13.02
    // Integer part = 13, Fractional part = 0.02 * 64 = 1
    mmio_write(UART_IBRD, 13);
    mmio_write(UART_FBRD, 1);
    
    // Set data format: 8 bits, no parity, 1 stop bit, enable FIFOs
    mmio_write(UART_LCRH, (1 << 4) | (1 << 5) | (1 << 6));
    
    // Enable UART, RX, and TX
    mmio_write(UART_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

// Send a character
void uart_putc(char c) {
    // Wait until transmit FIFO is not full
    while (mmio_read(UART_FR) & UART_FR_TXFF);
    
    // Write character to data register
    mmio_write(UART_DR, c);
}

// Send a string
void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

// Receive a character
char uart_getc(void) {
    // Wait until receive FIFO is not empty
    while (mmio_read(UART_FR) & UART_FR_RXFE);
    
    // Read character from data register
    return mmio_read(UART_DR) & 0xFF;
}

// Check if character is available
int uart_getc_async(void) {
    if (mmio_read(UART_FR) & UART_FR_RXFE) {
        return -1; // No character available
    }
    
    return mmio_read(UART_DR) & 0xFF;
}