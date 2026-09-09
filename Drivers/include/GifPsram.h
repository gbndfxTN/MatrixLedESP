#pragma once
#include <stdlib.h>
#include <stdint.h>
#include "esp_err.h"

#define CAP (2 * 1024 * 1024)

typedef struct {
    uint8_t *data;
    size_t cap;
    size_t len;
    volatile uint8_t state; // 0: libre, 1: en cours sur core 0 (download/decode), 2: en cours sur core 1 (affichage), 3: pret pour core 1
} buffer_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t initGifPsram(void);
uint8_t* getGifPsram(uint8_t core);
esp_err_t setGifPsramLen(uint8_t *psram, size_t len);
size_t getGifPsramLen(uint8_t *psram);
void setGifPsramState(uint8_t *psram, uint8_t state);
void resetGifPsram(uint8_t *psram);
void freeGifPsram(void);

#ifdef __cplusplus
}
#endif
