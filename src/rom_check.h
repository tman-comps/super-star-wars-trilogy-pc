/*
 * rom_check.h — load a user-supplied ROM and verify it is the exact version
 * this port supports, using its SHA-1 hash.
 *
 * Usage:
 *     RomImage rom;
 *     RomCheckResult r = rom_load_and_verify("ssw.sfc", GAME_SSW, &rom);
 *     if (r != ROM_OK) {
 *         show_error(rom_check_message(r, GAME_SSW));
 *         return 1;
 *     }
 *     ... use rom.data / rom.size ...
 *     rom_free(&rom);
 */
#ifndef ROM_CHECK_H
#define ROM_CHECK_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    GAME_SSW = 0,   /* Super Star Wars (USA, Rev 1)              */
    GAME_SESB,      /* Super Empire Strikes Back (USA, Rev 1)    */
    GAME_SROTJ,     /* Super Return of the Jedi (USA, Rev 1)     */
    GAME_COUNT
} GameId;

typedef enum {
    ROM_OK = 0,
    ROM_ERR_NOT_FOUND,      /* file missing or unreadable          */
    ROM_ERR_BAD_SIZE,       /* file is not a plausible SNES ROM    */
    ROM_ERR_WRONG_VERSION,  /* hash doesn't match the supported one */
    ROM_ERR_OUT_OF_MEMORY
} RomCheckResult;

typedef struct {
    uint8_t *data;  /* ROM contents, copier header already stripped */
    size_t   size;
} RomImage;

/* Loads the file, strips a 512-byte copier header if present, checks the
 * SHA-1 against the supported version for `game`. On ROM_OK, `out` owns
 * the data and must be released with rom_free(). */
RomCheckResult rom_load_and_verify(const char *path, GameId game, RomImage *out);

void rom_free(RomImage *rom);

/* Human-readable error text suitable for a message box. */
const char *rom_check_message(RomCheckResult result, GameId game);

/* Display name of a game, e.g. "Super Star Wars". */
const char *rom_game_name(GameId game);

/* Computes the SHA-1 of a buffer and writes it as 40 lowercase hex chars
 * plus a terminating NUL into `hex_out` (must be at least 41 bytes). */
void rom_sha1_hex(const uint8_t *data, size_t size, char hex_out[41]);

#endif /* ROM_CHECK_H */
