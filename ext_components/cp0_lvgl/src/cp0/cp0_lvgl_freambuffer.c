/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "hal_lvgl_bsp.h"
#include "lvgl/lvgl.h"
#include "commount.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "cp0_lvgl.h"
#include "../cp0_display_screenshot_contract.hpp"
#include <pthread.h>

static int find_st7789v_fbdev(char *dev_path, size_t buf_size)
{
    if (dev_path == NULL || buf_size == 0)
        return -1;

    if (access("/dev/fb_lcd", F_OK) == 0) {
        snprintf(dev_path, buf_size, "/dev/fb_lcd");
        return 0;
    }

    FILE *fp = fopen("/proc/fb", "r");
    if (fp != NULL) {
        char line[256];
        int fb_num = -1;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strstr(line, "fb_st7789v") != NULL || strstr(line, "st7789") != NULL) {
                if (sscanf(line, "%d", &fb_num) == 1)
                    break;
            }
        }
        fclose(fp);
        if (fb_num >= 0) {
            snprintf(dev_path, buf_size, "/dev/fb%d", fb_num);
            return 0;
        }
    }

    snprintf(dev_path, buf_size, "/dev/fb0");
    return 0;
}

#if !LV_USE_LINUX_DRM
/*
 * The CP0 LCD is a DRM SPI panel (panel-mipi-dbi); /dev/fb0 is the kernel's
 * fbdev emulation, which pushes mmap writes to the panel from a deferred-I/O
 * timer (HZ/20). That caps what reaches the glass at ~12-20 fps no matter how
 * fast LVGL renders. fsync() on the fbdev runs the pending flush right away,
 * so every finished LVGL frame goes out over SPI immediately.
 */
static int g_fb_sync_fd = -1;
static uint32_t g_fb_frame_pixels = 0;
static uint32_t g_fb_sync_min_pixels = 0;

/* Sum what each refresh flushes; only frames that change a large area (carousel
 * swipes, page transitions, scrolling) are pushed immediately. Small updates
 * such as the clock or the charging icon keep the kernel's batched 20 Hz pace,
 * so an idle home screen does not stream SPI transfers. */
static void fbdev_frame_sync_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FLUSH_START) {
        const lv_area_t *area = (const lv_area_t *)lv_event_get_param(e);
        if (area != NULL)
            g_fb_frame_pixels += (uint32_t)lv_area_get_size(area);
    } else if (code == LV_EVENT_REFR_READY) {
        if (g_fb_frame_pixels >= g_fb_sync_min_pixels && g_fb_frame_pixels > 0 &&
            g_fb_sync_fd >= 0)
            fsync(g_fb_sync_fd);
        g_fb_frame_pixels = 0;
    }
}

static int env_int(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0')
        return fallback;
    return atoi(value);
}

static void fbdev_enable_frame_sync(lv_display_t *disp, const char *device)
{
    /* APPLAUNCH_FB_SYNC=0 restores the stock deferred-I/O pacing. */
    if (env_int("APPLAUNCH_FB_SYNC", 1) != 0) {
        g_fb_sync_fd = open(device, O_RDWR | O_CLOEXEC);
        if (g_fb_sync_fd >= 0) {
            /* APPLAUNCH_FB_SYNC_MIN_PCT: share of the screen a frame must
             * change to be pushed at once (default 12%, 0 = every frame). */
            int pct = env_int("APPLAUNCH_FB_SYNC_MIN_PCT", 12);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            const uint32_t screen = (uint32_t)lv_display_get_horizontal_resolution(disp) *
                                    (uint32_t)lv_display_get_vertical_resolution(disp);
            g_fb_sync_min_pixels = screen * (uint32_t)pct / 100u;
            lv_display_add_event_cb(disp, fbdev_frame_sync_cb, LV_EVENT_FLUSH_START, NULL);
            lv_display_add_event_cb(disp, fbdev_frame_sync_cb, LV_EVENT_REFR_READY, NULL);
            printf("Framebuffer frame sync: on (>= %d%% of the screen)\n", pct);
        } else {
            perror("Framebuffer frame sync: open");
        }
    }

    /* LVGL's default 33 ms period caps rendering and animation at ~30 fps. */
    const int period = env_int("APPLAUNCH_REFR_PERIOD_MS", 16);
    if (period >= 5 && period <= 100) {
        lv_timer_set_period(lv_display_get_refr_timer(disp), (uint32_t)period);
        lv_timer_t *anim_timer = lv_anim_get_timer();
        if (anim_timer != NULL)
            lv_timer_set_period(anim_timer, (uint32_t)period);
        printf("Display refresh period: %d ms\n", period);
    }
}
#endif

void init_freambuffer_disp()
{
    static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&init_mutex);
    if (lv_display_get_default() != NULL) {
        pthread_mutex_unlock(&init_mutex);
        return;
    }
#if LV_USE_LINUX_DRM
    const char *device = getenv("LV_LINUX_DRM_DEVICE");
    char *detected_device = NULL;
    if (device == NULL || device[0] == '\0') {
        detected_device = lv_linux_drm_find_device_path();
        device = detected_device;
    }

    if (device == NULL || device[0] == '\0') {
        fprintf(stderr, "Failed to find a connected DRM device\n");
        lv_free(detected_device);
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    printf("Using DRM device: %s\n", device);
    lv_display_t *disp = lv_linux_drm_create();
    if (disp == NULL) {
        fprintf(stderr, "Failed to create DRM display\n");
        lv_free(detected_device);
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    if (lv_linux_drm_set_file(disp, device, -1) != LV_RESULT_OK) {
        fprintf(stderr, "Failed to initialize DRM device: %s\n", device);
        lv_display_delete(disp);
        lv_free(detected_device);
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    lv_free(detected_device);

    printf("DRM resolution: %dx%d\n",
           (int)lv_display_get_horizontal_resolution(disp),
           (int)lv_display_get_vertical_resolution(disp));
#else
    lv_display_t *disp = lv_linux_fbdev_create();
    if (disp == NULL) {
        printf("Failed to create fbdev display!\n");
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    const char *device = getenv("LV_LINUX_FBDEV_DEVICE");
    char fbdev[64] = {0};
    if (device == NULL || device[0] == '\0') {
        find_st7789v_fbdev(fbdev, sizeof(fbdev));
        device = fbdev;
    }

    setenv("APPLAUNCH_LINUX_FBDEV_DEVICE", device, 1);
    setenv("LV_LINUX_FBDEV_DEVICE", device, 1);
    printf("Using framebuffer device: %s\n", device);
    lv_linux_fbdev_set_file(disp, device);
    if(0)
        lv_linux_fbdev_set_force_refresh(disp, true);
    fbdev_enable_frame_sync(disp, device);

    lv_coord_t w = lv_display_get_horizontal_resolution(disp);
    lv_coord_t h = lv_display_get_vertical_resolution(disp);
    printf("Framebuffer resolution: %dx%d\n", w, h);
#endif
    pthread_mutex_unlock(&init_mutex);
}
