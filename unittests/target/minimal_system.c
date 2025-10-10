/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

/* Minimal System Support for ESP Target Tests
 * Provides essential ESP-specific initialization and libc integration */

#include <stddef.h>
#include <stdint.h>  // For uintptr_t and uint32_t
#include <stdio.h>   // For proper stdio functions
#include <sys/stat.h> // For _fstat system call
#include <sys/reent.h> // For _reent structure

// Include esp-stub-lib headers for UART functions
#include <esp-stub-lib/rom_wrappers.h>

void stub_target_uart_init(uint8_t uart_num, uint32_t baudrate);

int putchar(int c)
{
    stub_lib_uart_tx_one_char((uint8_t)c);
    return c;
}

// Minimal system initialization
void __system_init(void)
{
    // Initialize BSS section (zero-fill)
    extern uint32_t _bss_start;
    extern uint32_t _bss_end;

    // Use safer pointer arithmetic to avoid linter warnings
    uintptr_t start = (uintptr_t)&_bss_start;
    uintptr_t end = (uintptr_t)&_bss_end;

    for (uintptr_t addr = start; addr < end; addr += sizeof(uint32_t)) {
        *(uint32_t*)addr = 0;
    }

    // Initialize UART using esp-stub-lib functions only
    // stub_target_uart_init(0, 115200);

    // time separation between esptool load-ram protocol and unity test execution
    stub_lib_delay_us(300000);  // 300ms

    // Send startup identification
    stub_lib_uart_tx_one_char('S');
    stub_lib_uart_tx_one_char('Y');
    stub_lib_uart_tx_one_char('S');
    stub_lib_uart_tx_one_char('I');
    stub_lib_uart_tx_one_char('N');
    stub_lib_uart_tx_one_char('I');
    stub_lib_uart_tx_one_char('T');
    stub_lib_uart_tx_one_char('\r');
    stub_lib_uart_tx_one_char('\n');

    // Flush UART to ensure data is sent immediately
    stub_lib_uart_tx_flush();
}

// Simple sbrk implementation for heap management
extern char end;  // Defined in linker script
static char *heap_ptr = &end;

void *_sbrk(int incr)
{
    char *prev_heap_ptr = heap_ptr;
    heap_ptr += incr;
    return (void *)prev_heap_ptr;
}

// Stub implementation for __getreent (needed by some libc functions)
// For bare metal systems, we can use a simple global reent structure
static struct _reent reent_data = _REENT_INIT(reent_data);

struct _reent *__getreent(void)
{
    return &reent_data;
}

// System call stubs to fix linker warnings
// These provide minimal implementations for bare metal ESP environment

int _close(int fd)
{
    (void)fd;  // Unused parameter
    return -1; // File operations not supported
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;  // Unused parameters
    (void)st;
    return -1; // File operations not supported
}

int _getpid(void)
{
    return 1; // Return a dummy PID for bare metal
}

int _kill(int pid, int sig)
{
    (void)pid; // Unused parameters
    (void)sig;
    return -1; // Signal operations not supported
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;     // Unused parameters
    (void)offset;
    (void)whence;
    return -1; // File operations not supported
}

int _read(int fd, void *buf, size_t count)
{
    (void)fd;    // Unused parameters
    (void)buf;
    (void)count;
    return -1; // Read operations not supported (except for Unity console output)
}

int _write(int fd, const void *buf, size_t count)
{
    // Handle stdout/stderr writes for printf support
    if (fd == 1 || fd == 2) { // stdout or stderr
        const char *cbuf = (const char *)buf;
        for (size_t i = 0; i < count; i++) {
            putchar(cbuf[i]);
        }
        return (int)count;
    }

    // Other file descriptors not supported
    (void)buf;
    (void)count;
    return -1;
}
