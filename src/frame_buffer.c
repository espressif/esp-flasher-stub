/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "frame_buffer.h"

/*
 * Two buffers: one being received into (s_rx_idx), one being processed (s_proc_idx).
 * Their roles are fixed — no dynamic search needed.
 * ~30% performance improvement over single-buffer for USB-Serial/JTAG flashing.
 */
/* Widest DMA cache line in use (ESP32-P4 L2). */
#define FRAME_BUFFER_CACHE_LINE 128U
/* Round up to whole cache lines so DMA invalidation of the buffer never touches
 * the CPU-owned metadata below (which would corrupt the frame state). */
#define FRAME_BUFFER_ALLOC \
    (((FRAME_BUFFER_SIZE) + FRAME_BUFFER_CACHE_LINE - 1U) & ~(FRAME_BUFFER_CACHE_LINE - 1U))

struct frame_buffer {
    uint8_t buffer[FRAME_BUFFER_ALLOC] __attribute__((aligned(FRAME_BUFFER_CACHE_LINE)));
    volatile size_t frame_length;
    volatile bool frame_complete;
    volatile bool frame_error;
};

static struct frame_buffer s_buffers[2];
static volatile uint8_t s_rx_idx = 0;
static volatile uint8_t s_proc_idx = 0;

void frame_buffer_mark_error(void)
{
    s_buffers[s_rx_idx].frame_error = true;
}

uint8_t *frame_buffer_acquire(void)
{
    if (s_buffers[s_rx_idx].frame_complete || s_buffers[s_rx_idx].frame_error) {
        uint8_t other = (uint8_t)(1U - s_rx_idx);
        if (s_buffers[other].frame_complete || s_buffers[other].frame_error) {
            return NULL;
        }
        s_rx_idx = other;
    }
    s_buffers[s_rx_idx].frame_length = 0;
    return s_buffers[s_rx_idx].buffer;
}

void frame_buffer_mark_complete(size_t len)
{
    struct frame_buffer *rx_buf = &s_buffers[s_rx_idx];
    if (len > FRAME_BUFFER_SIZE) {
        rx_buf->frame_error = true;
        return;
    }
    rx_buf->frame_length = len;
    rx_buf->frame_complete = true;
}

enum frame_buffer_state frame_buffer_get_state(void)
{
    uint8_t other = (uint8_t)(1U - s_rx_idx);
    if (s_buffers[other].frame_error)       {
        s_proc_idx = other;
        return FRAME_BUFFER_STATE_ERROR;
    }
    if (s_buffers[s_rx_idx].frame_error)    {
        s_proc_idx = s_rx_idx;
        return FRAME_BUFFER_STATE_ERROR;
    }
    if (s_buffers[other].frame_complete)    {
        s_proc_idx = other;
        return FRAME_BUFFER_STATE_COMPLETE;
    }
    if (s_buffers[s_rx_idx].frame_complete) {
        s_proc_idx = s_rx_idx;
        return FRAME_BUFFER_STATE_COMPLETE;
    }
    return FRAME_BUFFER_STATE_IDLE;
}

const uint8_t *frame_buffer_get_data(size_t *length)
{
    *length = s_buffers[s_proc_idx].frame_length;
    return s_buffers[s_proc_idx].buffer;
}

/*
 * Releases ONLY the buffer last selected by frame_buffer_get_state()
 * (s_proc_idx). It must never touch the buffer the ISR/DMA is currently
 * filling (s_rx_idx), is idempotent (clearing an already-clear buffer is a
 * no-op), and never re-arms. The transport layer relies on all three
 * properties: READ_FLASH's post-process releases the command frame early and
 * the main loop releases it again afterwards, so a double release must be
 * harmless. Do not add re-arming or s_rx_idx handling here.
 */
void frame_buffer_reset(void)
{
    s_buffers[s_proc_idx].frame_length = 0;
    s_buffers[s_proc_idx].frame_complete = false;
    s_buffers[s_proc_idx].frame_error = false;
}
