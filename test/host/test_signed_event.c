// Host (gcc) tests for the EIP-712 encoding and signing of cold-chain events.
// The expected values come from the contract itself
// (contracts/test/FirmwareVector.t.sol), so passing here means the firmware
// and the contract agree byte for byte.
// Run: make -C test/host

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "event_payload.h"
#include "signed_event.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

// ---- Shared vector (FirmwareVector.t.sol) ----
static const uint64_t CHAIN_ID = 11155111; // Sepolia
static const char CONTRACT_HEX[] = "c0ffee00000000000000000000000000c0ffee00";
static const char DEVICE_ADDRESS_HEX[] = "f5a5e415061470a8b9137959180901aea72450a4";
static const char DOMAIN_SEPARATOR_HEX[] =
    "c992022eeeb889ba26d944cc6ae914837890e626fcbd65e34cdbe4ffec5819af";
static const char STRUCT_HASH_HEX[] =
    "e4a71dc55de5fea1f0fe51333bb5ad2082a781786798d9f9dd6337ab27279644";
static const char DIGEST_HEX[] =
    "fe87e48fa206d1338445822589ef6bf432ecc32277dae7ae5b1454fd8e30e3b5";
static const char SIGNATURE_HEX[] =
    "08a7c85b3031fc6059aec942042ef1e10e1bc7b17ecf9fa752a3a1fc44c74cb5"
    "5a2f3eca400efae2f3591feba1e1aad4e5dcd42b02381dad3d2a12883133eee8"
    "1c";

static void from_hex(const char *hex, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned int byte;
        sscanf(&hex[2 * i], "%2x", &byte);
        out[i] = (uint8_t)byte;
    }
}

static void device_key(uint8_t key[32])
{
    memset(key, 0, 32);
    key[29] = 0xC0; // 0xC0FFEE, the same key as DEVICE_PK in Foundry
    key[30] = 0xFF;
    key[31] = 0xEE;
}

static signed_event_t vector_event(void)
{
    signed_event_t ev = {
        .kind = SIGNED_EVENT_KIND_RECOVERY,
        .anomaly_type = 1, // ABOVE
        .start_time_ms = 1767225600000ULL,
        .end_time_ms = 1767225615000ULL,
        .temperature = 790,
        .peak = 1121,
        .counter = 42,
    };
    signed_event_id("box01", ev.box_id);
    signed_event_id("batch-demo-001", ev.batch_id);
    return ev;
}

static void test_keccak_known_value(void)
{
    puts("keccak256 of an empty id matches the known Ethereum value");
    uint8_t out[32], expected[32];
    signed_event_id("", out);
    from_hex("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470", expected, 32);
    CHECK(memcmp(out, expected, 32) == 0);
}

static void test_matches_contract(void)
{
    puts("domain separator, struct hash, digest and signature match the contract");
    uint8_t contract[20], expected[65], out[65];
    from_hex(CONTRACT_HEX, contract, 20);
    signed_event_t ev = vector_event();

    uint8_t domain[32];
    signed_event_domain_separator(CHAIN_ID, contract, domain);
    from_hex(DOMAIN_SEPARATOR_HEX, expected, 32);
    CHECK(memcmp(domain, expected, 32) == 0);

    signed_event_struct_hash(&ev, out);
    from_hex(STRUCT_HASH_HEX, expected, 32);
    CHECK(memcmp(out, expected, 32) == 0);

    uint8_t digest[32];
    signed_event_digest(domain, &ev, digest);
    from_hex(DIGEST_HEX, expected, 32);
    CHECK(memcmp(digest, expected, 32) == 0);

    // RFC 6979 is deterministic, so the firmware signature must equal the
    // one Foundry produces for the same key and digest.
    uint8_t key[32];
    device_key(key);
    CHECK(signed_event_sign(key, digest, out));
    from_hex(SIGNATURE_HEX, expected, SIGNED_EVENT_SIG_LEN);
    CHECK(memcmp(out, expected, SIGNED_EVENT_SIG_LEN) == 0);
}

static void test_device_address(void)
{
    puts("device address derived from the key matches the contract registration");
    uint8_t key[32], addr[20], expected[20];
    device_key(key);
    CHECK(signed_event_address(key, addr));
    from_hex(DEVICE_ADDRESS_HEX, expected, 20);
    CHECK(memcmp(addr, expected, 20) == 0);
}

static void test_signatures_are_low_s(void)
{
    puts("signatures always have low s and v in {27, 28}");
    // secp256k1 order / 2, big-endian
    uint8_t half_n[32];
    from_hex("7fffffffffffffffffffffffffffffff5d576e7357a4501ddfe92f46681b20a0", half_n, 32);

    uint8_t key[32], contract[20], domain[32], digest[32], sig[65];
    device_key(key);
    from_hex(CONTRACT_HEX, contract, 20);
    signed_event_domain_separator(CHAIN_ID, contract, domain);

    signed_event_t ev = vector_event();
    int seen_v[2] = { 0, 0 };
    for (uint32_t c = 0; c < 64; c++) {
        ev.counter = c;
        signed_event_digest(domain, &ev, digest);
        CHECK(signed_event_sign(key, digest, sig));
        CHECK(memcmp(&sig[32], half_n, 32) <= 0);
        CHECK(sig[64] == 27 || sig[64] == 28);
        seen_v[sig[64] - 27] = 1;
    }
    CHECK(seen_v[0] && seen_v[1]); // both parities show up over 64 signatures
}

static void test_changes_alter_digest(void)
{
    puts("changing any signed field changes the digest");
    uint8_t contract[20], domain[32], base[32], other[32];
    from_hex(CONTRACT_HEX, contract, 20);
    signed_event_domain_separator(CHAIN_ID, contract, domain);
    signed_event_t ev = vector_event();
    signed_event_digest(domain, &ev, base);

    signed_event_t t = ev;
    t.temperature = 500;
    signed_event_digest(domain, &t, other);
    CHECK(memcmp(base, other, 32) != 0);

    t = ev;
    t.counter = 43;
    signed_event_digest(domain, &t, other);
    CHECK(memcmp(base, other, 32) != 0);

    uint8_t other_domain[32];
    signed_event_domain_separator(CHAIN_ID + 1, contract, other_domain);
    signed_event_digest(other_domain, &ev, other);
    CHECK(memcmp(base, other, 32) != 0);
}

static void test_rejects_invalid_key(void)
{
    puts("an all-zero private key is rejected");
    uint8_t key[32] = { 0 }, digest[32] = { 1 }, sig[65], addr[20];
    CHECK(!signed_event_sign(key, digest, sig));
    CHECK(!signed_event_address(key, addr));
}

static void test_centi_degrees(void)
{
    puts("Celsius to centi-degrees conversion");
    CHECK(signed_event_centi_degrees(8.12f) == 812);
    CHECK(signed_event_centi_degrees(-20.005f) == -2001 || signed_event_centi_degrees(-20.005f) == -2000);
    CHECK(signed_event_centi_degrees(0.0f) == 0);
    CHECK(signed_event_centi_degrees(NAN) == INT16_MIN);
    CHECK(signed_event_centi_degrees(1000.0f) == INT16_MAX);
    CHECK(signed_event_centi_degrees(-1000.0f) == INT16_MIN + 1); // MIN reserved
}

// ---- event_payload: FSM event -> signed event -> MQTT JSON ----

// The FSM transition that corresponds to the shared vector event.
static fsm_event_t vector_fsm_event(void)
{
    fsm_event_t ev = {
        .type = FSM_EVT_RECOVERY,
        .anomaly = ANOMALY_ABOVE,
        .start_ms = 63500,    // monotonic; the epoch times are passed separately
        .duration_ms = 15000,
        .temperature = 7.90f,
        .peak = 11.21f,
    };
    return ev;
}

static void test_fsm_event_signs_like_contract(void)
{
    puts("FSM recovery event builds the vector event and the contract's signature");
    uint8_t box[32], batch[32], contract[20], domain[32], digest[32], sig[65], expected[65];
    signed_event_id("box01", box);
    signed_event_id("batch-demo-001", batch);
    from_hex(CONTRACT_HEX, contract, 20);
    signed_event_domain_separator(CHAIN_ID, contract, domain);

    fsm_event_t fsm_ev = vector_fsm_event();
    signed_event_t ev;
    event_payload_build(&fsm_ev, box, batch, 1767225600000ULL, 1767225615000ULL, 42, &ev);

    signed_event_t want = vector_event();
    CHECK(memcmp(&ev, &want, sizeof(ev)) == 0);

    uint8_t key[32];
    device_key(key);
    signed_event_digest(domain, &ev, digest);
    CHECK(signed_event_sign(key, digest, sig));
    from_hex(SIGNATURE_HEX, expected, SIGNED_EVENT_SIG_LEN);
    CHECK(memcmp(sig, expected, SIGNED_EVENT_SIG_LEN) == 0);
}

static void test_anomaly_and_offline_fields(void)
{
    puts("ANOMALY carries no end time; sensor offline maps to INT16_MIN");
    uint8_t box[32] = { 0 }, batch[32] = { 0 };
    fsm_event_t fsm_ev = {
        .type = FSM_EVT_ANOMALY,
        .anomaly = ANOMALY_SENSOR_OFFLINE,
        .start_ms = 95000,
        .temperature = NAN,
        .peak = NAN,
    };
    signed_event_t ev;
    event_payload_build(&fsm_ev, box, batch, 1767225695000ULL, 1767225699999ULL, 7, &ev);
    CHECK(ev.kind == SIGNED_EVENT_KIND_ANOMALY);
    CHECK(ev.anomaly_type == 2);
    CHECK(ev.start_time_ms == 1767225695000ULL);
    CHECK(ev.end_time_ms == 0);
    CHECK(ev.temperature == INT16_MIN);
    CHECK(ev.peak == INT16_MIN);
    CHECK(ev.counter == 7);
}

static void test_payload_json(void)
{
    puts("MQTT payload JSON has the contract field names and the signature");
    uint8_t sig[65];
    from_hex(SIGNATURE_HEX, sig, SIGNED_EVENT_SIG_LEN);
    signed_event_t ev = vector_event();

    char buf[512];
    int n = event_payload_json(&ev, sig, buf, sizeof(buf));
    const char *expected =
        "{\"boxId\":\"0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb\","
        "\"batchId\":\"0x36c5cfd13ef95136a333b8467f27e5810f10588f87c862682a58cbe406b12fad\","
        "\"kind\":1,\"anomalyType\":1,\"startTime\":1767225600000,\"endTime\":1767225615000,"
        "\"temperature\":790,\"peak\":1121,\"counter\":42,"
        "\"signature\":\"0x08a7c85b3031fc6059aec942042ef1e10e1bc7b17ecf9fa752a3a1fc44c74cb5"
        "5a2f3eca400efae2f3591feba1e1aad4e5dcd42b02381dad3d2a12883133eee81c\"}";
    CHECK(n == (int)strlen(expected));
    CHECK(strcmp(buf, expected) == 0);

    ev.temperature = INT16_MIN;
    n = event_payload_json(&ev, sig, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "\"temperature\":-32768,") != NULL);

    CHECK(event_payload_json(&ev, sig, buf, 100) == -1); // too small
}

static void test_parse_address(void)
{
    puts("contract address parsing");
    uint8_t addr[20], expected[20];
    from_hex(CONTRACT_HEX, expected, 20);
    CHECK(event_payload_parse_address("0xC0FFEE00000000000000000000000000c0FFEe00", addr));
    CHECK(memcmp(addr, expected, 20) == 0);
    CHECK(event_payload_parse_address(CONTRACT_HEX, addr)); // without 0x
    CHECK(memcmp(addr, expected, 20) == 0);
    CHECK(!event_payload_parse_address("", addr));
    CHECK(!event_payload_parse_address("0x1234", addr));
    CHECK(!event_payload_parse_address("0xG0ffee00000000000000000000000000c0ffee00", addr));
}

int main(void)
{
    test_keccak_known_value();
    test_matches_contract();
    test_device_address();
    test_signatures_are_low_s();
    test_changes_alter_digest();
    test_rejects_invalid_key();
    test_centi_degrees();
    test_fsm_event_signs_like_contract();
    test_anomaly_and_offline_fields();
    test_payload_json();
    test_parse_address();

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    puts("\nOK: all signed event tests passed");
    return 0;
}
