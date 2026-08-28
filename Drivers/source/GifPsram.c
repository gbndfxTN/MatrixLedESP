#include "GifPsram.h"
#include "esp_heap_caps.h"

/* Double buffer PSRAM :
 *  - le core 0 (download/decode) prend un buffer a l'etat libre (state 0),
 *    le passe a l'etat 1 (en cours core 0), ecrit dedans puis le passe
 *    a l'etat 3 (pret pour core 1).
 *  - le core 1 (HUB75) ne lit que les buffers a l'etat 3, les met a
 *    l'etat 2 pendant la lecture, puis les remet a l'etat 0.
 * Les deux buffers ne sont jamais touches en meme temps.
 */
static buffer_t PsramA;
static buffer_t PsramB;

esp_err_t initGifPsram(void) {
    PsramA.data = (uint8_t*)heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM);
    PsramB.data = (uint8_t*)heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM);
    PsramA.cap = CAP;
    PsramB.cap = CAP;
    PsramA.len = 0;
    PsramB.len = 0;
    PsramA.state = 0;
    PsramB.state = 0;

    if (PsramA.data == NULL || PsramB.data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static buffer_t *buffer_from_ptr(uint8_t *psram) {
    if (psram == PsramA.data) return &PsramA;
    if (psram == PsramB.data) return &PsramB;
    return NULL;
}

uint8_t* getGifPsram(uint8_t core) {
    if (core == 0) {
        if (PsramA.state == 0) return PsramA.data;
        if (PsramB.state == 0) return PsramB.data;
    } else {
        if (PsramA.state == 3) return PsramA.data;
        if (PsramB.state == 3) return PsramB.data;
    }
    return NULL;
}

esp_err_t setGifPsramLen(uint8_t *psram, size_t len) {
    buffer_t *buf = buffer_from_ptr(psram);
    if (!buf || len > buf->cap) return ESP_ERR_INVALID_ARG;
    buf->len = len;
    return ESP_OK;
}

size_t getGifPsramLen(uint8_t *psram) {
    buffer_t *buf = buffer_from_ptr(psram);
    return buf ? buf->len : 0;
}

void setGifPsramState(uint8_t *psram, uint8_t state) {
    buffer_t *buf = buffer_from_ptr(psram);
    if (buf) buf->state = state;
}

void resetGifPsram(uint8_t *psram) {
    buffer_t *buf = buffer_from_ptr(psram);
    if (!buf) return;
    buf->len = 0;
    buf->state = 0;
}

void freeGifPsram(void) {
    if (PsramA.state == 0) {
        heap_caps_free(PsramA.data);
        PsramA.data = NULL;
        PsramA.cap = 0;
        PsramA.len = 0;
    }
    if (PsramB.state == 0) {
        heap_caps_free(PsramB.data);
        PsramB.data = NULL;
        PsramB.cap = 0;
        PsramB.len = 0;
    }
}
