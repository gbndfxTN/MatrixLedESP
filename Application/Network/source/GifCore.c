#include "GifCore.h"
#include "HttpsCore.h"
#include "gifdec.h"
#include "GifPsram.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "GIFCORE";

static void scale_rgb24_to_rgb565_nearest(const uint8_t *src, int src_w, int src_h, uint16_t *dst, int dst_w, int dst_h) {
    for (int y = 0; y < dst_h; y++) {
        int src_y = (y * src_h) / dst_h;
        for (int x = 0; x < dst_w; x++) {
            int src_x = (x * src_w) / dst_w;

            int src_idx = (src_y * src_w + src_x) * 3;
            uint8_t r = src[src_idx + 0];
            uint8_t g = src[src_idx + 1];
            uint8_t b = src[src_idx + 2];
            uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            dst[y * dst_w + x] = rgb565;
        }
    }
}

#pragma pack(push, 1)
typedef struct {
    uint32_t id;
    uint8_t width;
    uint8_t height;
    uint16_t frame_count;
    uint16_t loop_count;
} BinHeader;
#pragma pack(pop)

bool download_gif(const char *url, const uint32_t id, const uint16_t loop_count, size_t *out_size) {
    size_t downloaded_size = 0;

    uint8_t *gif_data = download_gif_https(url, &downloaded_size);
    if (!gif_data) {
        ESP_LOGE(TAG, "Echec telechargement GIF");
        return false;
    }

    gd_GIF *gif = gd_open_gif(gif_data, downloaded_size);
    if (!gif) {
        ESP_LOGE(TAG, "Echec decodage GIF (%zu octets)", downloaded_size);
        heap_caps_free(gif_data);
        return false;
    }

    ESP_LOGI(TAG, "GIF valide: %dx%d", gif->width, gif->height);

    const int num_pixels = MATRIX_WIDTH * MATRIX_HEIGHT;
    const size_t frame_bytes = sizeof(uint16_t) + (size_t)num_pixels * sizeof(uint16_t);
    size_t bin_size = sizeof(BinHeader);

    uint8_t *bin_buffer = getGifPsram(0);
    if (!bin_buffer) {
        ESP_LOGE(TAG, "Echec allocation bin_buffer (aucun buffer PSRAM libre)");
        gd_close_gif(gif);
        heap_caps_free(gif_data);
        return false;
    }
    setGifPsramState(bin_buffer, 1); /* reservation : en cours sur core 0 */

    BinHeader *hdr = (BinHeader *)bin_buffer;
    hdr->id = id;
    hdr->width = (uint8_t)MATRIX_WIDTH;
    hdr->height = (uint8_t)MATRIX_HEIGHT;
    hdr->frame_count = 0;
    hdr->loop_count = loop_count;

    uint8_t *rgb_src = (uint8_t *)heap_caps_malloc((size_t)gif->width * gif->height * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint16_t *rgb_scaled = (uint16_t *)heap_caps_malloc((size_t)num_pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!rgb_src || !rgb_scaled) {
        ESP_LOGE(TAG, "Echec allocation memoire de decodage");
        heap_caps_free(rgb_src);
        heap_caps_free(rgb_scaled);
        gd_close_gif(gif);
        heap_caps_free(gif_data);
        resetGifPsram(bin_buffer); /* libere la reservation */
        return false;
    }

    uint16_t frame_counter = 0;
    while (gd_get_frame(gif) > 0) {
        if (bin_size + frame_bytes > CAP) {
            ESP_LOGE(TAG, "Trop de frames pour la capacite PSRAM (%u frames) - stop", frame_counter);
            break;
        }

        gd_render_frame(gif, rgb_src);
        scale_rgb24_to_rgb565_nearest(rgb_src, gif->width, gif->height, rgb_scaled, MATRIX_WIDTH, MATRIX_HEIGHT);

        uint16_t delay_ms = gif->gce.delay ? (gif->gce.delay * 10) : 100;
        memcpy(&bin_buffer[bin_size], &delay_ms, sizeof(uint16_t));
        memcpy(&bin_buffer[bin_size + sizeof(uint16_t)], rgb_scaled, (size_t)num_pixels * sizeof(uint16_t));
        bin_size += frame_bytes;
        frame_counter++;
        vTaskDelay(1);
    }

    if (frame_counter == 0) {
        ESP_LOGW(TAG, "Aucune frame decodee");
        heap_caps_free(rgb_src);
        heap_caps_free(rgb_scaled);
        gd_close_gif(gif);
        heap_caps_free(gif_data);
        resetGifPsram(bin_buffer); /* libere la reservation */
        return false;
    }

    hdr = (BinHeader *)bin_buffer;
    hdr->frame_count = frame_counter;

    setGifPsramLen(bin_buffer, bin_size);
    setGifPsramState(bin_buffer, 3); /* pret pour le core 1 (HUB75) */

    ESP_LOGI(TAG, "GIF decode: %u frames, %zu octets prets HUB75", frame_counter, bin_size);

    if (out_size) {
        *out_size = bin_size;
    }
    heap_caps_free(rgb_src);
    heap_caps_free(rgb_scaled);
    gd_close_gif(gif);
    heap_caps_free(gif_data);
    return true;
}
