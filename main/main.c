#include "NetworkCore.h"
#include "LittlefsCore.h"
#include "GifCore.h"
#include "TelegramCore.h"
#include "config.h"
#include "GifPsram.h"
#include "Hub75Core.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "MAIN";

static void task_gif(void *pvParameters) {
    ESP_LOGI(TAG, "Connexion WiFi...");
    if (!wifi_connect_simple()) {
        ESP_LOGE(TAG, "Wi-Fi non connecte. Redemarrage...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    ESP_LOGI(TAG, "Wi-Fi connecte");

    ESP_LOGI(TAG, "Montage LittleFS...");
    ESP_ERROR_CHECK(init_littlefs());

    char url[256];
    uint16_t loops;
    uint32_t id;

    ESP_LOGI(TAG, "En attente de commandes Telegram (long poll %d s)...",
             TELEGRAM_POLL_TIMEOUT_S);
    while (1) {
        if (gif_command_fetch(url, sizeof(url), &loops, &id)) {
            size_t bin_size = 0;
            if (download_gif(url, id, loops, &bin_size)) {
                ESP_LOGI(TAG, "GIF decodé (%zu octets) - en attente du core HUB75", bin_size);
            } else {
                ESP_LOGE(TAG, "Telechargement/decodage GIF echoue");
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

static void task_hub75_test(void *pvParameters) {
    ESP_LOGI(TAG, "Initialisation HUB75...");
    hub75_init();
    hub75_set_brightness(50);
    ESP_LOGI(TAG, "HUB75 pret - en attente du GIF dans la PSRAM");
    hub75_run();
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Allocation double buffer PSRAM...");
    ESP_ERROR_CHECK(initGifPsram());

    xTaskCreatePinnedToCore(task_gif, "gif", 8192, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(task_hub75_test, "hub75_test", 8192, NULL, 3, NULL, 1);
}
