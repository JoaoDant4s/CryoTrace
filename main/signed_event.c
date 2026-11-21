#include "signed_event.h"

#include <math.h>
#include <string.h>

#include "ecdsa.h"
#include "memzero.h"
#include "secp256k1.h"
#include "sha3.h"

#define WORD 32

static const char DOMAIN_TYPE[] =
    "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)";
static const char DOMAIN_NAME[] = "CryoTrace";
static const char DOMAIN_VERSION[] = "1";

// Must match CryoTrace.EVENT_TYPEHASH.
static const char EVENT_TYPE[] =
    "ColdChainEvent(bytes32 boxId,bytes32 batchId,uint8 kind,uint8 anomalyType,"
    "uint64 startTime,uint64 endTime,int16 temperature,int16 peak,uint32 counter)";

static void keccak_str(const char *s, uint8_t out[WORD])
{
    keccak_256((const uint8_t *)s, strlen(s), out);
}

// abi.encode of an unsigned integer: 32 bytes, big-endian, zero-padded.
static void put_uint(uint8_t word[WORD], uint64_t v)
{
    memset(word, 0, WORD);
    for (int i = WORD - 1; v != 0; i--) {
        word[i] = (uint8_t)v;
        v >>= 8;
    }
}

// abi.encode of a signed integer: two's complement sign-extended to 32 bytes.
static void put_int(uint8_t word[WORD], int64_t v)
{
    memset(word, v < 0 ? 0xFF : 0x00, WORD);
    uint64_t u = (uint64_t)v;
    for (int i = WORD - 1; i >= WORD - 8; i--) {
        word[i] = (uint8_t)u;
        u >>= 8;
    }
}

void signed_event_id(const char *text, uint8_t out[32])
{
    keccak_str(text, out);
}

void signed_event_domain_separator(uint64_t chain_id, const uint8_t contract[20], uint8_t out[32])
{
    uint8_t enc[5 * WORD];
    keccak_str(DOMAIN_TYPE, &enc[0 * WORD]);
    keccak_str(DOMAIN_NAME, &enc[1 * WORD]);
    keccak_str(DOMAIN_VERSION, &enc[2 * WORD]);
    put_uint(&enc[3 * WORD], chain_id);
    memset(&enc[4 * WORD], 0, WORD - 20);
    memcpy(&enc[4 * WORD + WORD - 20], contract, 20);
    keccak_256(enc, sizeof(enc), out);
}

void signed_event_struct_hash(const signed_event_t *ev, uint8_t out[32])
{
    uint8_t enc[10 * WORD];
    keccak_str(EVENT_TYPE, &enc[0 * WORD]);
    memcpy(&enc[1 * WORD], ev->box_id, WORD);
    memcpy(&enc[2 * WORD], ev->batch_id, WORD);
    put_uint(&enc[3 * WORD], ev->kind);
    put_uint(&enc[4 * WORD], ev->anomaly_type);
    put_uint(&enc[5 * WORD], ev->start_time_ms);
    put_uint(&enc[6 * WORD], ev->end_time_ms);
    put_int(&enc[7 * WORD], ev->temperature);
    put_int(&enc[8 * WORD], ev->peak);
    put_uint(&enc[9 * WORD], ev->counter);
    keccak_256(enc, sizeof(enc), out);
}

void signed_event_digest(const uint8_t domain_separator[32], const signed_event_t *ev, uint8_t out[32])
{
    uint8_t enc[2 + 2 * WORD] = { 0x19, 0x01 };
    memcpy(&enc[2], domain_separator, WORD);
    signed_event_struct_hash(ev, &enc[2 + WORD]);
    keccak_256(enc, sizeof(enc), out);
}

bool signed_event_sign(const uint8_t private_key[32], const uint8_t digest[32],
                       uint8_t sig[SIGNED_EVENT_SIG_LEN])
{
    uint8_t recovery = 0;
    if (ecdsa_sign_digest(&secp256k1, private_key, digest, sig, &recovery, NULL) != 0) {
        return false;
    }
    // Bit 1 (R.x >= curve order) has probability ~2^-128 and has no
    // Ethereum encoding; treat it as a failure.
    if (recovery > 1) {
        memzero(sig, SIGNED_EVENT_SIG_LEN);
        return false;
    }
    sig[64] = 27 + recovery;
    return true;
}

bool signed_event_address(const uint8_t private_key[32], uint8_t address[20])
{
    uint8_t pub[65];
    if (ecdsa_get_public_key65(&secp256k1, private_key, pub) != 0) {
        return false;
    }
    uint8_t hash[WORD];
    keccak_256(&pub[1], 64, hash); // skip the 0x04 prefix
    memcpy(address, &hash[WORD - 20], 20);
    return true;
}

int16_t signed_event_centi_degrees(float celsius)
{
    if (isnan(celsius)) {
        return INT16_MIN;
    }
    float centi = roundf(celsius * 100.0f);
    // INT16_MIN is reserved for "no reading".
    if (centi > INT16_MAX) {
        return INT16_MAX;
    }
    if (centi < INT16_MIN + 1) {
        return INT16_MIN + 1;
    }
    return (int16_t)centi;
}
