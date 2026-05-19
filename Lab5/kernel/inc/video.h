#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

/*
 * Framebuffer base address on Orange Pi RV2.
 * U-Boot initializes the framebuffer at this physical address.
 */
#define FB_BASE   0x7f700000UL

/*
 * Framebuffer resolution.
 * These values should match the resolution initialized by U-Boot.
 */
#define FB_WIDTH  1920
#define FB_HEIGHT 1080

/*
 * Bytes per pixel.
 * 4 bytes means each pixel is stored as a 32-bit value.
 */
#define FB_BPP    4

/*
 * Cache block size used for flushing framebuffer memory.
 */
#define CACHE_BLOCK_SIZE 64

/*
 * Flush one cache block that contains the given address.
 *
 * In video_bmp_display(), memcpy() writes pixel data to the framebuffer.
 * However, the CPU may keep those writes in the data cache first.
 * The HDMI/display hardware reads the framebuffer from DRAM, not from
 * the CPU cache. Therefore, after writing the framebuffer, we must flush
 * the cache so the latest pixel data is written back to DRAM.
 *
 * `.word 0x0025200F` is the raw machine encoding of the RISC-V
 * `cbo.flush a0` instruction. It is used here because some toolchains
 * may not recognize the `cbo.flush` mnemonic directly.
 *
 * Before executing the raw instruction, the target address is moved into
 * register a0, because this encoding flushes the cache block addressed by a0.
 */
#define cbo_flush(start)                \
    ({                                  \
        asm volatile("mv a0, %0\n\t"    \
                     ".word 0x0025200F" \
                     :                  \
                     : "r"(start)       \
                     : "memory", "a0"); \
    })

void video_bmp_display(unsigned int* bmp_image, int width, int height);

#endif