// Host replacement for components/trezor_crypto/port/random_esp.c.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/random.h>

void random_buffer(uint8_t *buf, size_t len)
{
    if (getrandom(buf, len, 0) != (ssize_t)len) {
        abort();
    }
}
