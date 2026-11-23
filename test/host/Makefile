SRC_DIR    := ../../main
TREZOR_DIR := ../../components/trezor_crypto/crypto
# Pure-C firmware modules (no ESP-IDF); headers are included by name.
SRC_DIRS   := $(addprefix $(SRC_DIR)/,sensor monitoring signing messaging)
CFLAGS     := -std=gnu11 -Wall -Wextra -Werror -O1 $(addprefix -I,$(SRC_DIRS))

# Third-party sources: built as upstream, without the project's -Werror.
TREZOR_SRCS  := $(addprefix $(TREZOR_DIR)/,bignum.c ecdsa.c hmac.c hmac_drbg.c memzero.c \
                rand.c rfc6979.c secp256k1.c sha2.c sha3.c)
TREZOR_FLAGS := -std=gnu11 -O1 -w -ffunction-sections -fdata-sections

MAIN_SRCS   := test_main.c $(SRC_DIR)/monitoring/filter.c $(SRC_DIR)/monitoring/fsm.c \
               $(SRC_DIR)/sensor/sensor_sim.c
SIGNED_SRCS := test_signed_event.c $(SRC_DIR)/signing/signed_event.c \
               $(SRC_DIR)/signing/event_payload.c random_host.c
BATCH_SRCS  := test_batch_message.c $(SRC_DIR)/messaging/batch_message.c
OUTBOX_SRCS := test_event_outbox.c $(SRC_DIR)/messaging/event_outbox.c
HEADERS     := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.h))

.PHONY: test clean
test: build/test_main build/test_signed_event build/test_batch_message build/test_event_outbox
	./build/test_main
	./build/test_signed_event
	./build/test_batch_message
	./build/test_event_outbox

build/test_main: $(MAIN_SRCS) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(MAIN_SRCS) -o $@ -lm

build/libtrezor.a: $(TREZOR_SRCS)
	@mkdir -p build/trezor
	cd build/trezor && $(CC) $(TREZOR_FLAGS) -I../../$(TREZOR_DIR) $(addprefix ../../,$(TREZOR_SRCS)) -c
	ar rcs $@ build/trezor/*.o

build/test_signed_event: $(SIGNED_SRCS) $(HEADERS) build/libtrezor.a
	$(CC) $(CFLAGS) -I$(TREZOR_DIR) $(SIGNED_SRCS) build/libtrezor.a -o $@ -lm -Wl,--gc-sections

build/test_batch_message: $(BATCH_SRCS) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(BATCH_SRCS) -o $@

build/test_event_outbox: $(OUTBOX_SRCS) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(OUTBOX_SRCS) -o $@

clean:
	rm -rf build
