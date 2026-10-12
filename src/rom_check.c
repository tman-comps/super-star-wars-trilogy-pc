/*
 * rom_check.c — see rom_check.h.
 *
 * Contains a small self-contained SHA-1 implementation so there are no
 * extra dependencies.
 */
#include "rom_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Supported ROMs.
 *
 * SHA-1 of each game's *headerless* ROM (printed by tools/romhash.c; on
 * Windows `Get-FileHash .\ssw.sfc -Algorithm SHA1` gives the same value
 * for a dump without a 512-byte copier header).
 * An empty hash would skip the version check.
 * ---------------------------------------------------------------------- */
static const struct {
    const char *name;
    const char *version;
    const char *sha1;
} k_games[GAME_COUNT] = {
    [GAME_SSW]   = { "Super Star Wars",                           "USA, Rev 1", "834b5c6b82b1ee22cc583075334800b51269624b" },
    [GAME_SESB]  = { "Super Star Wars: The Empire Strikes Back",  "USA, Rev 1", "82bf21e0ba427bcc9c6390bf028dfd76de3c94d3" },
    [GAME_SROTJ] = { "Super Star Wars: Return of the Jedi",       "USA, Rev 1", "b8dca1cd8b2ac99e46078fc91fd42ee336e761e4" },
};

/* ============================ SHA-1 ==================================== */

typedef struct {
    uint32_t h[5];
    uint64_t len_bits;
    uint8_t  buf[64];
    size_t   buf_len;
} Sha1Ctx;

static uint32_t rol32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_block(Sha1Ctx *c, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
               (uint32_t)p[i * 4 + 2] << 8 | (uint32_t)p[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;                    k = 0xCA62C1D6; }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol32(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_init(Sha1Ctx *c) {
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len_bits = 0; c->buf_len = 0;
}

static void sha1_update(Sha1Ctx *c, const uint8_t *data, size_t n) {
    c->len_bits += (uint64_t)n * 8;
    while (n > 0) {
        size_t take = 64 - c->buf_len;
        if (take > n) take = n;
        memcpy(c->buf + c->buf_len, data, take);
        c->buf_len += take; data += take; n -= take;
        if (c->buf_len == 64) { sha1_block(c, c->buf); c->buf_len = 0; }
    }
}

static void sha1_final(Sha1Ctx *c, uint8_t out[20]) {
    uint64_t len = c->len_bits;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    pad = 0;
    while (c->buf_len != 56) sha1_update(c, &pad, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(len >> (56 - i * 8));
    sha1_update(c, lenb, 8);
    for (int i = 0; i < 5; i++) {
        out[i * 4]     = (uint8_t)(c->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->h[i]);
    }
}

void rom_sha1_hex(const uint8_t *data, size_t size, char hex_out[41]) {
    static const char digits[] = "0123456789abcdef";
    Sha1Ctx c;
    uint8_t digest[20];
    sha1_init(&c);
    sha1_update(&c, data, size);
    sha1_final(&c, digest);
    for (int i = 0; i < 20; i++) {
        hex_out[i * 2]     = digits[digest[i] >> 4];
        hex_out[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex_out[40] = '\0';
}

/* =========================== Loading =================================== */

#define COPIER_HEADER_SIZE 512
#define MIN_ROM_SIZE (256 * 1024)        /* 2 Mbit  */
#define MAX_ROM_SIZE (8 * 1024 * 1024)   /* 64 Mbit */

RomCheckResult rom_load_and_verify(const char *path, GameId game, RomImage *out) {
    out->data = NULL;
    out->size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return ROM_ERR_NOT_FOUND;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ROM_ERR_NOT_FOUND; }
    long file_size = ftell(f);
    if (file_size < 0) { fclose(f); return ROM_ERR_NOT_FOUND; }
    rewind(f);

    size_t size = (size_t)file_size;
    size_t skip = (size % 1024 == COPIER_HEADER_SIZE) ? COPIER_HEADER_SIZE : 0;
    size -= skip;

    if (size < MIN_ROM_SIZE || size > MAX_ROM_SIZE || size % 1024 != 0) {
        fclose(f);
        return ROM_ERR_BAD_SIZE;
    }

    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) { fclose(f); return ROM_ERR_OUT_OF_MEMORY; }

    if ((skip && fseek(f, (long)skip, SEEK_SET) != 0) ||
        fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        return ROM_ERR_NOT_FOUND;
    }
    fclose(f);

    const char *expected = k_games[game].sha1;
    if (expected[0] != '\0') {
        char actual[41];
        rom_sha1_hex(data, size, actual);
        if (strcmp(actual, expected) != 0) {
            free(data);
            return ROM_ERR_WRONG_VERSION;
        }
    }

    out->data = data;
    out->size = size;
    return ROM_OK;
}

void rom_free(RomImage *rom) {
    free(rom->data);
    rom->data = NULL;
    rom->size = 0;
}

const char *rom_game_name(GameId game) {
    return (game >= 0 && game < GAME_COUNT) ? k_games[game].name : "Unknown game";
}

const char *rom_check_message(RomCheckResult result, GameId game) {
    static char msg[512];
    const char *name = rom_game_name(game);
    const char *ver  = (game >= 0 && game < GAME_COUNT) ? k_games[game].version : "";

    switch (result) {
    case ROM_OK:
        snprintf(msg, sizeof msg, "%s ROM OK.", name);
        break;
    case ROM_ERR_NOT_FOUND:
        snprintf(msg, sizeof msg,
                 "Couldn't find or read the ROM for %s.\n\n"
                 "Put your own dump of the cartridge next to the program "
                 "and make sure it has the expected file name.", name);
        break;
    case ROM_ERR_BAD_SIZE:
        snprintf(msg, sizeof msg,
                 "The file for %s doesn't look like an SNES ROM "
                 "(unexpected size). Is it zipped or the wrong file?", name);
        break;
    case ROM_ERR_WRONG_VERSION:
        snprintf(msg, sizeof msg,
                 "This ROM is not the supported version of %s.\n\n"
                 "Required: %s. Other regions and revisions won't work.",
                 name, ver);
        break;
    case ROM_ERR_OUT_OF_MEMORY:
        snprintf(msg, sizeof msg, "Out of memory while loading %s.", name);
        break;
    }
    return msg;
}
