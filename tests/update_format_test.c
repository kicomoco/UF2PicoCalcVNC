#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "update_format.h"

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8));
}

int main(int argc, char **argv) {
    uint32_t a[3], b[3];
    assert(update_version("v1.0.10", a) && update_version("1.0.9", b));
    assert(update_compare(a, b) > 0 && update_compare(b, a) < 0 && !update_compare(a, a));
    assert(update_version("2.0.0", b) && update_compare(a, b) < 0);
    const char *invalid[] = {"", "v", "1", "1.0", "1.0.0.0", "1.0.1-rc1", "-1.0.0", "1.0.4294967296"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
        assert(!update_version(invalid[i], a));

    uint8_t digest[32];
    const char *sum = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  app.uf2\n";
    assert(update_checksum(sum, "app.uf2", digest) && digest[0] == 1 && digest[31] == 0xef);
    assert(!update_checksum(sum, "wrong.uf2", digest));
    assert(!update_checksum("1234", "app.uf2", digest));
    char bad[160];
    snprintf(bad, sizeof(bad), "%sother data", sum);
    assert(!update_checksum(bad, "app.uf2", digest));
    strcpy(bad, sum); bad[0] = 'g';
    assert(!update_checksum(bad, "app.uf2", digest));

    uint8_t block[512] = {0};
    uint32_t count;
    put32(block, 0x0a324655); put32(block + 4, 0x9e5d5157);
    put32(block + 8, 0xa000); put32(block + 12, 0x10ffff00);
    put32(block + 16, 256); put32(block + 24, 2);
    put32(block + 28, 0xe48bff57); put32(block + 508, 0x0ab16f30);
    assert(update_uf2_block(block, 0, &count) && count == 0);
    put32(block + 8, 0x2000); put32(block + 12, 0x10000000);
    put32(block + 28, 0xe48bff59);
    assert(update_uf2_block(block, 1, &count) && count == 2);
    put32(block + 20, 1);
    assert(update_uf2_block(block, 2, &count));
    assert(!update_uf2_block(block, 3, &count));
    put32(block + 28, 0xe48bff56);
    assert(!update_uf2_block(block, 2, &count));
    put32(block + 28, 0xe48bff59); put32(block + 12, 0x20000000);
    assert(!update_uf2_block(block, 2, &count));
    put32(block + 12, 0x10000000); block[508] ^= 1;
    assert(!update_uf2_block(block, 2, &count));

    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb");
        assert(f);
        uint32_t n = 0;
        size_t bytes;
        while ((bytes = fread(block, 1, sizeof(block), f))) {
            assert(bytes == sizeof(block) && update_uf2_block(block, n, &count));
            ++n;
        }
        assert(!ferror(f) && n > 1 && n == count + 1);
        fclose(f);
    }
    puts("Update version, checksum and UF2 validation tests passed.");
}
