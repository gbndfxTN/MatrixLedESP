idf_component_register(
    SRCS source/NetworkCore.c source/LittlefsCore.c source/HttpsCore.c source/GifCore.c source/gifdec.c source/TelegramCore.c
    INCLUDE_DIRS include
    REQUIRES Drivers esp_wifi esp_event esp_netif lwip esp_http_client mbedtls nvs_flash joltwallet__littlefs espressif__cjson
)
