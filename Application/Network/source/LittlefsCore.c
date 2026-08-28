#include "LittlefsCore.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "stdio.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "Littlefs";

esp_err_t init_littlefs(void) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Échec du montage ou du formatage");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Partition 'littlefs' introuvable dans la table");
        } else {
            ESP_LOGE(TAG, "Erreur LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Montage OK — Espace : %d Ko / %d Ko libres", 
                 (total - used) / 1024, total / 1024);
    }

    return ESP_OK;
}


uint8_t* load_file(const char *filepath, size_t *out_size) {
    if (filepath == NULL || out_size == NULL) {
        ESP_LOGE(TAG, "Paramètres d'entrée invalides");
        return NULL;
    }

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Impossible d'ouvrir le fichier : %s", filepath);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size == 0) {
        ESP_LOGE(TAG, "Le fichier %s est vide", filepath);
        fclose(f);
        return NULL; 
    }

    uint8_t *buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, size, f);
    fclose(f);

    if (bytes_read != size) {
        ESP_LOGE(TAG, "Erreur lors de la lecture (%zu/%zu octets lus)", bytes_read, size);
        heap_caps_free(buffer);
        return NULL;
    }

    *out_size = size;
    ESP_LOGI(TAG, "Fichier %s chargé en PSRAM (%zu octets)", filepath, size);

    return buffer;
}

esp_err_t write_file(const char *filepath, const void *data, size_t size) {
    if (filepath == NULL || data == NULL || size == 0) {
        ESP_LOGE(TAG, "Arguments invalides pour l'écriture");
        return ESP_ERR_INVALID_ARG;
    }

    char path_copy[256];
    strncpy(path_copy, filepath, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    for (char *p = path_copy + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path_copy, 0755);
            *p = '/';
        }
    }

    FILE *f = fopen(filepath, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Impossible d'ouvrir le fichier en écriture : %s", filepath);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        ESP_LOGE(TAG, "Écriture incomplète (%zu/%zu octets écrites)", written, size);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Fichier enregistré : %s (%zu octets)", filepath, size);
    return ESP_OK;
}

esp_err_t delete_file(const char *filepath) {
    if (filepath == NULL) {
        ESP_LOGE(TAG, "Chemin invalide pour la suppression");
        return ESP_ERR_INVALID_ARG;
    }

    if (remove(filepath) != 0) {
        ESP_LOGE(TAG, "Impossible de supprimer le fichier : %s", filepath);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Fichier supprimé avec succès : %s", filepath);
    return ESP_OK;
}
