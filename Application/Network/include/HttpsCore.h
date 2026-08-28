#pragma once

#include <stdlib.h>
#include <stdint.h>

uint8_t* download_gif_https(const char *url, size_t *out_size);
