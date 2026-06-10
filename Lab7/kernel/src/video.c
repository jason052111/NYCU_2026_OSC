#include "video.h"
#include "tool.h"
#include "vm.h"
/*
 * Flush a memory range from CPU cache to DRAM.
 * This is required after writing to the framebuffer, because the display
 * hardware reads image data from DRAM, not from the CPU cache.
 */
static void flush_dcache(void* addr, unsigned long len) {
    unsigned long start = (unsigned long)addr & ~(CACHE_BLOCK_SIZE - 1);
    unsigned long end = (unsigned long)addr + len;
    /*
     * Make sure memory writes are completed before flushing cache lines.
     */
    __sync_synchronize();

    for (unsigned long line = start; line < end; line += CACHE_BLOCK_SIZE) {
        cbo_flush(line);
    }
    /*
     * Make sure cache flush operations are completed.
     */
    __sync_synchronize();
}
/*
 * Display a BMP image on the framebuffer.
 *
 * The image is copied row by row into the center of the screen.
 * After each row is written, the corresponding framebuffer memory is
 * flushed so the HDMI display hardware can read the latest pixel data.
 */
void video_bmp_display(unsigned int* bmp_image, int width, int height) {
    unsigned int* fb = (unsigned int*)PA2VA(FB_BASE_PA);
    /*
     * Calculate the top-left position so the image is centered.
     */
    int start_x = (FB_WIDTH - width) / 2;
    int start_y = (FB_HEIGHT - height) / 2;

    for (int y = 0; y < height; y++) {
        // Destination address of the current image row in the framebuffer.
        void* dst = fb + (start_y + y) * FB_WIDTH + start_x; 
        // Copy one row of pixels into the framebuffer.
        memcpy(dst,
               bmp_image + y * width,
               width * sizeof(unsigned int));
        // Flush the written row so the display hardware sees the update.
        flush_dcache(dst, width * sizeof(unsigned int));
    }
}
/*
 * Write data from a kernel buffer into framebuffer memory.
 * offset specifies the byte position relative to the framebuffer base.
 * After copying the data, the corresponding cache range is flushed so
 * the display hardware can observe the updated framebuffer contents.
 */
int video_framebuffer_write(unsigned long offset, const void* buf, size_t len) {
    if (buf == 0) return -1;
    unsigned long fb_size = (unsigned long)FB_WIDTH * FB_HEIGHT * FB_BPP;
    if (offset > fb_size || len > fb_size - offset) return -1;

    void* dst = (void*)(PA2VA(FB_BASE_PA) + offset);
    mem_copy(dst, buf, len);
    flush_dcache(dst, len);
    return (int)len;
}
/*
 * Return basic framebuffer information through a kernel structure.
 * sys_ioctl() later copies this structure back into the requesting
 * user process through copy_to_user().
 */
int video_framebuffer_get_info(struct framebuffer_info* info)
{
    if (info == 0) return -1;
    info->width = FB_WIDTH;
    info->height = FB_HEIGHT;
    info->bpp = FB_BPP;
    return 0;
}

