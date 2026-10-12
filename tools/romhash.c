/*
 * romhash — prints the SHA-1 of an SNES ROM with any 512-byte copier
 * header removed. Use it to fill in the hashes in src/rom_check.c.
 *
 * Build (from the repo root):
 *     cl /I src tools\romhash.c src\rom_check.c        (Visual Studio)
 *     gcc -I src tools/romhash.c src/rom_check.c -o romhash
 *
 * Run:
 *     romhash ssw.sfc
 */
#include <stdio.h>
#include <stdlib.h>
#include "rom_check.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: romhash <rom file>\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);

    unsigned char *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "read failed\n");
        return 1;
    }
    fclose(f);

    size_t skip = (n % 1024 == 512) ? 512 : 0;
    char hex[41];
    rom_sha1_hex(buf + skip, (size_t)n - skip, hex);

    printf("%s\n", hex);
    if (skip) printf("(512-byte copier header was stripped)\n");
    free(buf);
    return 0;
}
