// Host tests for the batch announcement the relayer publishes
// (relayer/cryotrace_relayer/batches.py). The message below is the exact
// output of the relayer's Assignment.payload().
// Run: make -C test/host

#include <stdio.h>
#include <string.h>

#include "batch_message.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void from_hex(const char *hex, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        sscanf(hex + 2 * i, "%2hhx", &out[i]);
    }
}

// keccak256("box01") and keccak256("batch-demo-002")
static const char BOX_HEX[] = "8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb";
static const char BATCH_HEX[] = "95b6f167b7073aa6792eb310f0947030189de8b8a68002b6763ebf89fbe02954";

static const char RELAYER_MESSAGE[] =
    "{\"boxId\": \"0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb\", "
    "\"batchId\": \"0x95b6f167b7073aa6792eb310f0947030189de8b8a68002b6763ebf89fbe02954\", "
    "\"blockNumber\": 12190999, "
    "\"txHash\": \"0xabababababababababababababababababababababababababababababababab\"}";

static void test_topic(void)
{
    printf("topic\n");
    uint8_t box[32];
    from_hex(BOX_HEX, box, 32);
    char topic[BATCH_TOPIC_SIZE];
    int n = batch_message_topic(box, topic, sizeof(topic));
    CHECK(n == BATCH_TOPIC_SIZE - 1);
    CHECK(strcmp(topic, "cryotrace/0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb/batch") == 0);
    CHECK(batch_message_topic(box, topic, sizeof(topic) - 1) == -1);
}

static void test_parses_relayer_message(void)
{
    printf("parses the relayer's message\n");
    uint8_t box[32], expected[32], batch[32] = {0};
    from_hex(BOX_HEX, box, 32);
    from_hex(BATCH_HEX, expected, 32);
    // Not NUL-terminated, as delivered by MQTT: pass the length without the terminator.
    CHECK(batch_message_parse(RELAYER_MESSAGE, strlen(RELAYER_MESSAGE), box, batch) == BATCH_MSG_OK);
    CHECK(memcmp(batch, expected, 32) == 0);

    const char compact[] =
        "{\"batchId\":\"0x95B6F167B7073AA6792EB310F0947030189DE8B8A68002B6763EBF89FBE02954\","
        "\"boxId\":\"0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb\"}";
    memset(batch, 0, 32);
    CHECK(batch_message_parse(compact, strlen(compact), box, batch) == BATCH_MSG_OK);
    CHECK(memcmp(batch, expected, 32) == 0);
}

static void test_rejects(void)
{
    printf("rejects other boxes and malformed messages\n");
    uint8_t box[32], batch[32];
    from_hex(BOX_HEX, box, 32);
    uint8_t untouched[32];
    memset(batch, 0x5a, 32);
    memcpy(untouched, batch, 32);

    uint8_t other_box[32];
    memcpy(other_box, box, 32);
    other_box[0] ^= 1;
    CHECK(batch_message_parse(RELAYER_MESSAGE, strlen(RELAYER_MESSAGE), other_box, batch) == BATCH_MSG_OTHER_BOX);

    CHECK(batch_message_parse("", 0, box, batch) == BATCH_MSG_EMPTY);
    const char *bad[] = {
        "not json",
        "{\"boxId\": \"0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb\"}", // no batch
        "{\"boxId\": \"0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb\", "
        "\"batchId\": \"0x1234\"}",                                                           // short
        "{\"boxId\": \"0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb\", "
        "\"batchId\": \"0xzz b6f167b7073aa6792eb310f0947030189de8b8a68002b6763ebf89fbe02954\"}", // not hex
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(batch_message_parse(bad[i], strlen(bad[i]), box, batch) == BATCH_MSG_MALFORMED);
    }
    // Truncated in the middle of the value: must not read past len.
    CHECK(batch_message_parse(RELAYER_MESSAGE, 120, box, batch) == BATCH_MSG_MALFORMED);
    CHECK(memcmp(batch, untouched, 32) == 0);
}

int main(void)
{
    test_topic();
    test_parses_relayer_message();
    test_rejects();
    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("all batch message tests passed\n");
    return 0;
}
