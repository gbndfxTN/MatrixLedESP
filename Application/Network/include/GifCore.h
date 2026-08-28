#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

bool download_gif(const char *url, const uint32_t id, const uint16_t loop_count, size_t *out_size);
