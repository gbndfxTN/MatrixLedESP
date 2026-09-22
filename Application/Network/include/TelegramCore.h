#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Recupere une commande GIF depuis le bot Telegram (poll getUpdates).
 * Retourne true uniquement si un nouveau message contenant une URL https
 * valide est disponible. */
bool gif_command_fetch(char *url_out, size_t url_len, uint16_t *loops_out, uint32_t *id_out);

#ifdef __cplusplus
}
#endif
