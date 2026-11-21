// Platform RNG required by trezor-crypto (declared in rand.h).
// Signing itself is deterministic (RFC 6979); randomness is only used to
// blind the computation against side-channel attacks.

#include <stddef.h>
#include <stdint.h>

#include "esp_random.h"

void random_buffer(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
}
