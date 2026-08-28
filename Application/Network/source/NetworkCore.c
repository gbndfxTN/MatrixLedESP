#include "NetworkCore.h"
#include "secrets.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

static const char *TAG = "WIFI_SIMPLE";
static volatile bool s_is_connected = false;

static const char *const s_months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int month_index(const char *m) {
    for (int i = 0; i < 12; i++) {
        if (strncmp(m, s_months[i], 3) == 0) return i;
    }
    return 0;
}

static void set_time_to_compile_time(void) {
    struct tm tm = {0};
    int day, year, hour, min, sec;
    char month[4];
    sscanf(__DATE__, "%3s %d %d", month, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);

    tm.tm_mday = day;
    tm.tm_mon = month_index(month);
    tm.tm_year = year - 1900;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    time_t t = mktime(&tm);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    ESP_LOGW(TAG, "NTP absent: heure initialisee a la compilation (%s %s)", __DATE__, __TIME__);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        ESP_LOGW(TAG, "Deconnecte, nouvelle tentative...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi Connecte ! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_is_connected = true;
    }
}

bool wifi_connect_simple(void) {
    s_is_connected = false;

    // 1. Initialisation NVS (OBLIGATOIRE pour le Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialisation réseau
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Force le WPA2
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    /* Le stream WebSocket privilegie le debit et la latence. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connexion a '%s'...", WIFI_SSID);

    // Attente max de 10 secondes (100 x 100ms)
    int timeout_ms = 10000;
    while (!s_is_connected && timeout_ms > 0) {
        usleep(100000);
        timeout_ms -= 100;
    }

    if (!s_is_connected) return false;

    // Le bundle de certificats HTTPS exige une horloge valide.
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = { 0 };
    int sync_timeout_ms = 10000;
    while (timeinfo.tm_year < (2020 - 1900) && sync_timeout_ms > 0) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2020 - 1900)) break;
        vTaskDelay(pdMS_TO_TICKS(100));
        sync_timeout_ms -= 100;
    }

    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGW(TAG, "Synchronisation NTP impossible: utilisation de l'heure de compilation");
        set_time_to_compile_time();
    } else {
        ESP_LOGI(TAG, "Heure synchronisee pour HTTPS");
    }

    return true;
}
