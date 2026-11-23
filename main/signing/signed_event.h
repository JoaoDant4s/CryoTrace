#pragma once

#include <stdbool.h>
#include <stdint.h>

// EIP-712 encoding and signing of the cold-chain events recorded by the
// CryoTrace contract (contracts/src/CryoTrace.sol). The layout below must
// match the contract's ColdChainEvent byte for byte.
// Plain C (no ESP-IDF) so it can be tested on the host.

#define SIGNED_EVENT_SIG_LEN 65 // r (32) || s (32) || v (1, 27 or 28)

typedef enum {
    SIGNED_EVENT_KIND_ANOMALY = 0,  // NORMAL -> ANOMALY
    SIGNED_EVENT_KIND_RECOVERY = 1, // ANOMALY -> NORMAL
} signed_event_kind_t;

typedef struct {
    uint8_t box_id[32];     // keccak256 of the box name, e.g. "box01"
    uint8_t batch_id[32];   // keccak256 of the batch name
    uint8_t kind;           // signed_event_kind_t
    uint8_t anomaly_type;   // anomaly_type_t (fsm.h)
    uint64_t start_time_ms; // epoch ms, 0 = clock not synced
    uint64_t end_time_ms;   // epoch ms, 0 for SIGNED_EVENT_KIND_ANOMALY
    int16_t temperature;    // centi-degrees C, INT16_MIN = no reading
    int16_t peak;           // centi-degrees C
    uint32_t counter;       // device counter, never reused
} signed_event_t;

// keccak256 of a UTF-8 id, e.g. "box01" -> boxId.
void signed_event_id(const char *text, uint8_t out[32]);

// EIP-712 domain separator for name "CryoTrace", version "1".
void signed_event_domain_separator(uint64_t chain_id, const uint8_t contract[20], uint8_t out[32]);

void signed_event_struct_hash(const signed_event_t *ev, uint8_t out[32]);

// Digest the device signs: keccak256(0x1901 || domain separator || struct hash).
void signed_event_digest(const uint8_t domain_separator[32], const signed_event_t *ev, uint8_t out[32]);

// Deterministic (RFC 6979) secp256k1 signature with low s, as the contract
// requires. Returns false if the key is invalid or the signature cannot be
// expressed with v = 27/28.
bool signed_event_sign(const uint8_t private_key[32], const uint8_t digest[32],
                       uint8_t sig[SIGNED_EVENT_SIG_LEN]);

// Ethereum address of a private key, registered in the contract at provisioning.
bool signed_event_address(const uint8_t private_key[32], uint8_t address[20]);

// Celsius to centi-degrees, rounded and clamped; NAN -> INT16_MIN.
int16_t signed_event_centi_degrees(float celsius);
