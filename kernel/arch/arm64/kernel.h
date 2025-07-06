#ifndef KERNEL_H
#define KERNEL_H

// ARM64 specific definitions
#define UART_BASE 0x09000000
#define UART_DR   0x00
#define UART_FR   0x18
#define UART_IBRD 0x24
#define UART_FBRD 0x28
#define UART_LCRH 0x2C
#define UART_CR   0x30

// UART flag register bits
#define UART_FR_TXFF (1 << 5)  // Transmit FIFO full
#define UART_FR_RXFE (1 << 4)  // Receive FIFO empty

// Function declarations
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char* str);
char uart_getc(void);
int uart_getc_async(void);

// Kernel entry point
void kernel_main(void);

#endif // KERNEL_H 