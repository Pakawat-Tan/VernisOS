#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

// UART definitions
#define UART0_BASE    0x09000000
#define UART_DR       (UART0_BASE + 0x00)  // Data register
#define UART_FR       (UART0_BASE + 0x18)  // Flag register
#define UART_IBRD     (UART0_BASE + 0x24)  // Integer baud rate divisor
#define UART_FBRD     (UART0_BASE + 0x28)  // Fractional baud rate divisor
#define UART_LCRH     (UART0_BASE + 0x2C)  // Line control register
#define UART_CR       (UART0_BASE + 0x30)  // Control register

// UART flags
#define UART_FR_TXFF  (1 << 5)  // Transmit FIFO full
#define UART_FR_RXFE  (1 << 4)  // Receive FIFO empty

// Generic timer definitions
#define CNTFRQ_EL0    "cntfrq_el0"
#define CNTVCT_EL0    "cntvct_el0"

// Memory definitions
#define KERNEL_START  0x40000000
#define KERNEL_SIZE   0x00100000  // 1MB
#define MEMORY_SIZE   0x08000000  // 128MB

// Function prototypes
void kernel_main(void);
void process_command(char *cmd);
void show_system_info(void);
void show_memory_info(void);
void show_uptime(void);
void system_reboot(void);

// UART functions
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);

// System functions
void memory_init(void);
void timer_init(void);
uint64_t get_system_time(void);

// Utility functions
int strcmp(const char *s1, const char *s2);
char* itoa(uint64_t num, int base);

// Assembly functions
extern void enable_interrupts(void);
extern void disable_interrupts(void);

#endif // KERNEL_H