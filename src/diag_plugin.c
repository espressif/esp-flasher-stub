/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * Diag plugin — compiled as a separate binary loaded at runtime by esptool.
 *
 * Provides a single command: DIAG_LOG_READ (0xDE).
 *
 * On first invocation the plugin installs s_logf_impl() into the base stub's
 * stub_logf function pointer.  From that point on, every STUB_LOGF() call in
 * the base stub is routed through the plugin's mini-printf into the ring buffer.
 *
 * Wire format — DIAG_LOG_READ response:
 *   value    : total bytes written since stub start (u32 LE);
 *              host uses this to detect ring-buffer overflow between polls.
 *   data[]   : up to MAX_RESPONSE_DATA_SIZE (64) bytes of log content, oldest first.
 *   data_size: number of valid bytes in data[]; 0 means the buffer is empty.
 *
 * Host usage: call DIAG_LOG_READ in a loop until data_size == 0.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include "commands.h"
#include "command_handler.h"
#include "plugin_table.h"

/* ---- Ring buffer --------------------------------------------------------- */

#define DIAG_LOG_BUF_SIZE 512U  /* must be a power of two */

/* All BSS — zeroed by esp_main at startup, no initialised .data allowed. */
static uint32_t s_write_pos;              /* total bytes ever written      */
static uint32_t s_read_pos;              /* total bytes consumed by host   */
static uint8_t  s_buf[DIAG_LOG_BUF_SIZE];

static void s_buf_write(const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        s_buf[s_write_pos & (DIAG_LOG_BUF_SIZE - 1)] = (uint8_t)data[i];
        s_write_pos++;
        if ((s_write_pos - s_read_pos) > DIAG_LOG_BUF_SIZE) {
            s_read_pos = s_write_pos - DIAG_LOG_BUF_SIZE;
        }
    }
}

static void s_buf_putc(char c)
{
    s_buf_write(&c, 1);
}

/* ---- Mini-printf --------------------------------------------------------- */
/*
 * Supported specifiers: %s %d %u %x (with optional zero-pad width, e.g. %08x)
 *                       %c %%
 * No floating point, no length modifiers beyond zero-pad width.
 */
static void s_logf_impl(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            s_buf_write(fmt, 1);
            fmt++;
            continue;
        }
        fmt++;  /* skip '%' */

        int width = 0;
        int zero_pad = (*fmt == '0');
        if (zero_pad) {
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }

        char tmp[12];
        size_t n;

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                /* "(null)" — character constants avoid .rodata */
                s_buf_putc('('); s_buf_putc('n'); s_buf_putc('u');
                s_buf_putc('l'); s_buf_putc('l'); s_buf_putc(')');
                break;
            }
            size_t slen = 0;
            while (s[slen]) {
                slen++;
            }
            s_buf_write(s, slen);
            break;
        }
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            if (v < 0) {
                s_buf_putc('-');
                v = -v;
            }
            n = 0;
            uint32_t uv = (uint32_t)v;
            do {
                tmp[n++] = (char)('0' + uv % 10);
                uv /= 10;
            } while (uv);
            for (size_t i = 0, j = n - 1; i < j; i++, j--) {
                char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
            s_buf_write(tmp, n);
            break;
        }
        case 'u': {
            uint32_t uv = va_arg(ap, uint32_t);
            n = 0;
            do {
                tmp[n++] = (char)('0' + uv % 10);
                uv /= 10;
            } while (uv);
            for (size_t i = 0, j = n - 1; i < j; i++, j--) {
                char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
            s_buf_write(tmp, n);
            break;
        }
        case 'x': {
            uint32_t uv = va_arg(ap, uint32_t);
            n = 0;
            do {
                unsigned nib = uv & 0xfu;
                tmp[n++] = (char)(nib < 10u ? '0' + nib : 'a' + nib - 10u);
                uv >>= 4;
            } while (uv);
            while (zero_pad && n < (size_t)width) {
                tmp[n++] = '0';
            }
            for (size_t i = 0, j = n - 1; i < j; i++, j--) {
                char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
            s_buf_write(tmp, n);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            s_buf_putc(c);
            break;
        }
        case '%':
            s_buf_putc('%');
            break;
        default:
            s_buf_putc('%');
            s_buf_write(fmt, 1);
            break;
        }
        fmt++;
    }

    va_end(ap);
}

/* ---- Base-stub function pointer installation ----------------------------- */

/*
 * BASE_STUB_LOGF_PTR is supplied via --defsym at link time.
 * Its value is the address of stub_logf in the base stub's BSS.
 * Writing through it installs s_logf_impl without any runtime symbol lookup.
 */
extern void (*base_stub_logf_ptr)(const char *fmt, ...);

static void s_install_logf(void)
{
    static uint8_t s_installed;
    if (s_installed) {
        return;
    }
    s_installed = 1;
    base_stub_logf_ptr = s_logf_impl;
}

/* ---- Command handler ----------------------------------------------------- */

int diag_plugin_log_read(uint8_t command, const uint8_t *data, uint32_t len,
                         struct command_response_data *resp)
{
    (void)command;
    (void)data;
    (void)len;

    s_install_logf();

    uint32_t available = s_write_pos - s_read_pos;
    uint32_t to_read = available < MAX_RESPONSE_DATA_SIZE ? available : MAX_RESPONSE_DATA_SIZE;

    for (uint32_t i = 0; i < to_read; i++) {
        resp->data[i] = s_buf[s_read_pos & (DIAG_LOG_BUF_SIZE - 1)];
        s_read_pos++;
    }

    resp->data_size = (uint16_t)to_read;
    resp->value = s_write_pos;
    return RESPONSE_SUCCESS;
}
