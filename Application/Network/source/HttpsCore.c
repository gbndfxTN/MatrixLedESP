#include "HttpsCore.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

static const char *TAG = "HTTPS";

#define GIF_MAX_DOWNLOAD_SIZE (4 * 1024 * 1024)
#define GIF_INITIAL_CHUNK     (32 * 1024)
#define GIF_READ_RETRIES      3


static int resolve_host_from_url(const char *url, char *host, size_t host_len) {
    const char *p = url + 8;
    const char *end = p;
    while (*end && *end != '/' && *end != ':') end++;
    size_t len = (size_t)(end - p);
    if (len == 0 || len >= host_len) return -1;
    memcpy(host, p, len);
    host[len] = '\0';
    return 0;
}

uint8_t* download_gif_https(const char *url, size_t *out_size) {
    *out_size = 0;

    if (!url || strncmp(url, "https://", 8) != 0) {
        ESP_LOGE(TAG, "URL refuse: seul HTTPS est accepte");
        return NULL;
    }

    char host[128];
    if (resolve_host_from_url(url, host, sizeof(host)) != 0) {
        ESP_LOGE(TAG, "Impossible d'extraire l'hote de l'URL");
        return NULL;
    }

    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    int dns_ret = getaddrinfo(host, "443", &hints, &res);
    if (dns_ret != 0 || !res) {
        ESP_LOGE(TAG, "Echec resolution DNS de '%s' (err=%d)", host, dns_ret);
        if (res) freeaddrinfo(res);
        return NULL;
    }
    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    char ip_str[16];
    strncpy(ip_str, inet_ntoa(addr->sin_addr), sizeof(ip_str) - 1);
    ip_str[sizeof(ip_str) - 1] = '\0';
    ESP_LOGI(TAG, "DNS OK pour '%s': %s", host, ip_str);

    int probe_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (probe_sock < 0) {
        ESP_LOGE(TAG, "Impossible de creer la socket TCP");
        freeaddrinfo(res);
        return NULL;
    }
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(probe_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(probe_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int tcp_ret = connect(probe_sock, res->ai_addr, res->ai_addrlen);
    int tcp_errno = errno;
    close(probe_sock);
    freeaddrinfo(res);
    if (tcp_ret != 0) {
        ESP_LOGE(TAG, "Echec connexion TCP vers %s (%s):443 (errno=%d)", host, ip_str, tcp_errno);
        return NULL;
    }
    ESP_LOGI(TAG, "TCP OK vers %s:443 (heap interne libre: %u)", host,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
        .max_redirection_count = 5,
        .buffer_size = 4096,
        .user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Echec init client HTTP");
        return NULL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        time_t now_t = time(NULL);
        struct tm tm_now;
        localtime_r(&now_t, &tm_now);
        unsigned free_int = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGE(TAG, "Échec TLS vers %s (%s): %s | annee=%d heap_int=%u",
                 host, ip_str, esp_err_to_name(err), tm_now.tm_year + 1900, free_int);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP %d, taille=%d", status_code, content_length);

    if (status_code < 200 || status_code >= 300) {
        ESP_LOGE(TAG, "Reponse HTTP invalide: %d", status_code);
        esp_http_client_cleanup(client);
        return NULL;
    }

    uint8_t *psram_buffer = NULL;
    size_t capacity = 0;
    size_t total_read = 0;

    if (content_length > 0) {
        if ((size_t)content_length > GIF_MAX_DOWNLOAD_SIZE) {
            ESP_LOGE(TAG, "GIF trop volumineux: %d octets (max %d)", content_length, GIF_MAX_DOWNLOAD_SIZE);
            esp_http_client_cleanup(client);
            return NULL;
        }
        capacity = (size_t)content_length;
        psram_buffer = (uint8_t *)heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM);
        if (!psram_buffer) {
            ESP_LOGE(TAG, "Mémoire PSRAM insuffisante pour %d octets", content_length);
            esp_http_client_cleanup(client);
            return NULL;
        }

        int zero_reads = 0;
        while (total_read < capacity) {
            int read_len = esp_http_client_read(client, (char *)psram_buffer + total_read, capacity - total_read);
            if (read_len < 0) {
                ESP_LOGE(TAG, "Erreur de lecture HTTP: %d", read_len);
                break;
            }
            if (read_len == 0) {
                if (++zero_reads >= GIF_READ_RETRIES) break;
                continue;
            }
            zero_reads = 0;
            total_read += (size_t)read_len;
        }

        esp_http_client_cleanup(client);

        if (total_read != capacity) {
            ESP_LOGE(TAG, "Téléchargement interrompu (%d/%d octets lus)", (int)total_read, (int)capacity);
            heap_caps_free(psram_buffer);
            return NULL;
        }
    } else {
        capacity = GIF_INITIAL_CHUNK;
        psram_buffer = (uint8_t *)heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM);
        if (!psram_buffer) {
            ESP_LOGE(TAG, "Mémoire PSRAM insuffisante pour %d octets", (int)capacity);
            esp_http_client_cleanup(client);
            return NULL;
        }

        ESP_LOGI(TAG, "Reponse chunked (ou sans Content-Length), lecture par blocs");
        int zero_reads = 0;
        while (1) {
            if (total_read == capacity) {
                if (capacity >= GIF_MAX_DOWNLOAD_SIZE) {
                    ESP_LOGE(TAG, "GIF trop volumineux (> %d octets)", GIF_MAX_DOWNLOAD_SIZE);
                    esp_http_client_cleanup(client);
                    heap_caps_free(psram_buffer);
                    return NULL;
                }
                size_t new_capacity = capacity * 2;
                if (new_capacity > GIF_MAX_DOWNLOAD_SIZE) new_capacity = GIF_MAX_DOWNLOAD_SIZE;
                uint8_t *new_buffer = (uint8_t *)heap_caps_realloc(psram_buffer, new_capacity, MALLOC_CAP_SPIRAM);
                if (!new_buffer) {
                    ESP_LOGE(TAG, "Mémoire PSRAM insuffisante lors du realloc (%d octets)", (int)new_capacity);
                    esp_http_client_cleanup(client);
                    heap_caps_free(psram_buffer);
                    return NULL;
                }
                psram_buffer = new_buffer;
                capacity = new_capacity;
            }

            int read_len = esp_http_client_read(client, (char *)psram_buffer + total_read, capacity - total_read);
            if (read_len < 0) {
                ESP_LOGE(TAG, "Erreur de lecture HTTP");
                esp_http_client_cleanup(client);
                heap_caps_free(psram_buffer);
                return NULL;
            }
            if (read_len == 0) {
                if (++zero_reads >= GIF_READ_RETRIES) break;
                continue;
            }
            zero_reads = 0;
            total_read += (size_t)read_len;
        }

        esp_http_client_cleanup(client);

        if (total_read == 0) {
            ESP_LOGE(TAG, "Corps de reponse vide");
            heap_caps_free(psram_buffer);
            return NULL;
        }
    }

    *out_size = total_read;
    ESP_LOGI(TAG, "GIF téléchargé avec succès en PSRAM (%d octets)", (int)total_read);
    return psram_buffer;
}
