#pragma once

#include "esp_err.h"
#include <stdlib.h>

esp_err_t init_littlefs(void);
uint8_t* load_file(const char *filepath, size_t *out_size);
esp_err_t write_file(const char *filepath, const void *data, size_t size);
esp_err_t delete_file(const char *filepath);


