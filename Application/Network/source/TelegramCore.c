#include "TelegramCore.h"
#include "config.h"
#include "secrets.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "TELEGRAM";

#define TELEGRAM_BUF_SIZE 2048

static uint32_t s_offset = 0;
static bool s_offset_ready = false;

static esp_err_t api_get_json(const char *url, char *buf, size_t buf_len) {
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = (TELEGRAM_POLL_TIMEOUT_S + 10) * 1000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "Telegram repond HTTP %d", status);
            err = ESP_FAIL;
        } else {
            int total = 0;
            while (total < (int)buf_len - 1) {
                int n = esp_http_client_read(client, buf + total, buf_len - 1 - total);
                if (n <= 0) {
                    break;
                }
                total += n;
            }
            buf[total] = '\0';
            err = (total > 0) ? ESP_OK : ESP_FAIL;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

/* getUpdates?offset=-1 renvoie le dernier update connu: permet d'ignorer
 * les messages empiles pendant que le device etait eteint. */
static bool bootstrap_offset(void) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s/getUpdates?offset=-1&limit=1",
             TELEGRAM_API_BASE, TELEGRAM_BOT_TOKEN);

    static char buf[TELEGRAM_BUF_SIZE];
    if (api_get_json(url, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return false;
    }

    bool ok = false;
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsArray(result) && cJSON_GetArraySize(result) > 0) {
        cJSON *upd = cJSON_GetArrayItem(result, 0);
        cJSON *jid = cJSON_GetObjectItemCaseSensitive(upd, "update_id");
        if (cJSON_IsNumber(jid)) {
            s_offset = (uint32_t)jid->valuedouble + 1;
            ok = true;
        }
    } else {
        /* File vide: aucun message envoye au bot. */
        s_offset = 0;
        ok = true;
    }

    cJSON_Delete(root);
    return ok;
}

static bool extract_url(const char *text, char *url_out, size_t url_len, uint16_t *loops_out) {
    const char *start = strstr(text, "http");
    if (!start) {
        return false;
    }

    size_t i = 0;
    while (start[i] && !isspace((unsigned char)start[i]) && i < url_len - 1) {
        url_out[i] = start[i];
        i++;
    }
    url_out[i] = '\0';

    if (strncmp(url_out, "https://", 8) != 0) {
        return false;
    }

    /* Token optionnel apres l'URL: "https://.../x.gif 5" => 5 boucles. */
    *loops_out = DISPLAY_GIF_LOOPS;
    const char *after = start + strlen(url_out);
    while (*after && isspace((unsigned char)*after)) {
        after++;
    }
    if (isdigit((unsigned char)*after)) {
        long v = strtol(after, NULL, 10);
        if (v > 0 && v <= 0xFFFF) {
            *loops_out = (uint16_t)v;
        }
    }
    return true;
}

bool gif_command_fetch(char *url_out, size_t url_len, uint16_t *loops_out, uint32_t *id_out) {
    if (!s_offset_ready) {
        s_offset_ready = bootstrap_offset();
        if (!s_offset_ready) {
            return false;
        }
        ESP_LOGI(TAG, "Poll Telegram pret (offset=%lu)", (unsigned long)s_offset);
    }

    char url[512];
    snprintf(url, sizeof(url), "%s%s/getUpdates?offset=%lu&limit=1&timeout=%d",
             TELEGRAM_API_BASE, TELEGRAM_BOT_TOKEN,
             (unsigned long)s_offset, TELEGRAM_POLL_TIMEOUT_S);

    static char buf[TELEGRAM_BUF_SIZE];
    if (api_get_json(url, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON invalide: %s", buf);
        return false;
    }

    bool ok = false;
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsArray(result) && cJSON_GetArraySize(result) > 0) {
        cJSON *upd = cJSON_GetArrayItem(result, 0);
        cJSON *jid = cJSON_GetObjectItemCaseSensitive(upd, "update_id");
        cJSON *msg = cJSON_GetObjectItemCaseSensitive(upd, "message");
        cJSON *text = msg ? cJSON_GetObjectItemCaseSensitive(msg, "text") : NULL;

        if (cJSON_IsNumber(jid)) {
            uint32_t update_id = (uint32_t)jid->valuedouble;
            s_offset = update_id + 1;

            if (cJSON_IsString(text) && text->valuestring &&
                extract_url(text->valuestring, url_out, url_len, loops_out)) {
                *id_out = update_id;
                ok = true;
                ESP_LOGI(TAG, "Nouvelle commande id=%lu loops=%u url=%s",
                         (unsigned long)update_id, *loops_out, url_out);
            } else {
                ESP_LOGI(TAG, "Message ignore (pas d'URL https): %s",
                         (cJSON_IsString(text) && text->valuestring) ? text->valuestring : "(vide)");
            }
        }
    }

    cJSON_Delete(root);
    return ok;
}
