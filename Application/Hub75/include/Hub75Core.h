#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


void hub75_init(void);
void hub75_run(void);
void hub75_set_brightness(uint8_t pct);
#ifdef __cplusplus
}
#endif
