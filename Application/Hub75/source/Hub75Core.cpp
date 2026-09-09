#include "Hub75Core.h"
#include "config.h"
#include "GifPsram.h"
#include "hub75.h"
#include "esp_log.h"
#include <string.h>
#include <stddef.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HUB75";

static Hub75Driver *s_driver = nullptr;

#pragma pack(push, 1)
typedef struct {
    uint32_t id;
    uint8_t width;
    uint8_t height;
    uint16_t frame_count;
    uint16_t loop_count;
} hub75_bin_header_t;
#pragma pack(pop)

static bool hub75_driver_init(void)
{
    Hub75Config config{};
    config.panel_width = MATRIX_WIDTH;
    config.panel_height = MATRIX_HEIGHT;
    config.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
    config.shift_driver = Hub75ShiftDriver::FM6126A;
    config.layout_rows = 1;
    config.layout_cols = 1;
    config.layout = Hub75PanelLayout::HORIZONTAL;

    config.pins.r1 = HUB75_R1;
    config.pins.g1 = HUB75_G1;
    config.pins.b1 = HUB75_B1;
    config.pins.r2 = HUB75_R2;
    config.pins.g2 = HUB75_G2;
    config.pins.b2 = HUB75_B2;
    config.pins.a = HUB75_A;
    config.pins.b = HUB75_B;
    config.pins.c = HUB75_C;
    config.pins.d = HUB75_D;
    config.pins.e = HUB75_E;
    config.pins.lat = HUB75_LAT;
    config.pins.oe = HUB75_OE;
    config.pins.clk = HUB75_CLK;

    config.output_clock_speed = Hub75ClockSpeed::HZ_20M;
    config.min_refresh_rate = 100;
    config.latch_blanking = 1;
    config.double_buffer = true;
    config.clk_phase_inverted = false;
    config.brightness = (uint8_t)((DISPLAY_BRIGHTNESS * 255) / 100);

    s_driver = new Hub75Driver(config);
    if (!s_driver) {
        ESP_LOGE(TAG, "Allocation driver impossible");
        return false;
    }

    if (!s_driver->begin()) {
        ESP_LOGE(TAG, "Initialisation driver HUB75 impossible");
        delete s_driver;
        s_driver = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Driver HUB75 demarre");
    return true;
}

static void hub75_play_buffer(uint8_t *data, size_t bin_size)
{
    if (!data || bin_size < sizeof(hub75_bin_header_t)) {
        ESP_LOGE(TAG, "Buffer GIF invalide (%u octets)", (unsigned)bin_size);
        return;
    }
    const hub75_bin_header_t *header = (const hub75_bin_header_t *)data;
    if (header->width != MATRIX_WIDTH || header->height != MATRIX_HEIGHT) {
        ESP_LOGE(TAG, "Dimensions invalides: %dx%d", header->width, header->height);
        return;
    }

    const int num_pixels = MATRIX_WIDTH * MATRIX_HEIGHT;
    const size_t frame_bytes = sizeof(uint16_t) + (size_t)num_pixels * sizeof(uint16_t);
    const uint16_t frames = header->frame_count;
    uint16_t loops = header->loop_count;
    if (loops == 0) loops = 1;

    ESP_LOGI(TAG, "Lecture: %u frames, %d loops", frames, loops);

    for (uint16_t i = 0; i < loops; i++) {
        const uint8_t *cursor = data + sizeof(hub75_bin_header_t);

        for (uint16_t j = 0; j < frames; j++) {
            if ((size_t)(cursor - data) + frame_bytes > bin_size) {
                ESP_LOGW(TAG, "Frame tronquee - fin de lecture");
                return;
            }

            int64_t frame_start_us = esp_timer_get_time();

            uint16_t delay_ms;
            memcpy(&delay_ms, cursor, sizeof(delay_ms));
            const uint8_t *pixels = cursor + sizeof(delay_ms);
            cursor += frame_bytes;

            s_driver->draw_pixels(0, 0, MATRIX_WIDTH, MATRIX_HEIGHT, pixels, Hub75PixelFormat::RGB565, Hub75ColorOrder::RGB, false);
            s_driver->flip_buffer();

            int64_t delay_us = (int64_t)delay_ms * 1000;
            int64_t target_us = frame_start_us + delay_us;
            int64_t now_us = esp_timer_get_time();
            while (now_us < target_us) {
                int64_t remaining_us = target_us - now_us;
                if (remaining_us > 3 * 1000) {
                    vTaskDelay(1);
                }
                now_us = esp_timer_get_time();
            }
        }
    }
}

void hub75_init(void)
{
    if (!hub75_driver_init()) {
        ESP_LOGE(TAG, "Initialisation HUB75 impossible");
    }
}

void hub75_run(void)
{
    ESP_LOGI(TAG, "Attente d'un GIF dans la PSRAM");
    while (1) {
        uint8_t *data = getGifPsram(1);
        if (data) {
            ESP_LOGI(TAG, "GIF trouve dans la PSRAM");
            setGifPsramState(data, 2);
            hub75_play_buffer(data, getGifPsramLen(data));
            resetGifPsram(data);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void hub75_set_brightness(uint8_t pct)
{
    if (s_driver) {
        s_driver->set_brightness((uint8_t)((pct * 255) / 100));
    }
}
