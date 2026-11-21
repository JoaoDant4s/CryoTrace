SRC_DIR    := ../../main
TREZOR_DIR := ../../components/trezor_crypto/crypto
CFLAGS     := -std=gnu11 -Wall -Wextra -Werror -O1 -I$(SRC_DIR)

# Third-party sources: built as upstream, without the project's -Werror.
TREZOR_SRCS  := $(addprefix $(TREZOR_DIR)/,bignum.c ecdsa.c hmac.c hmac_drbg.c memzero.c \
                rand.c rfc6979.c secp256k1.c sha2.c sha3.c)
TREZOR_FLAGS := -std=gnu11 -O1 -w -ffunction-sections -fdata-sections

MAIN_SRCS   := test_main.c $(SRC_DIR)/filter.c $(SRC_DIR)/fsm.c $(SRC_DIR)/sensor_sim.c
SIGNED_SRCS := test_signed_event.c $(SRC_DIR)/signed_event.c random_host.c

.PHONY: test clean
test: build/test_main build/test_signed_event
	./build/test_main
	./build/test_signed_event

build/test_main: $(MAIN_SRCS) $(wildcard $(SRC_DIR)/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(MAIN_SRCS) -o $@ -lm

build/libtrezor.a: $(TREZOR_SRCS)
	@mkdir -p build/trezor
	cd build/trezor && $(CC) $(TREZOR_FLAGS) -I../../$(TREZOR_DIR) $(addprefix ../../,$(TREZOR_SRCS)) -c
	ar rcs $@ build/trezor/*.o

build/test_signed_event: $(SIGNED_SRCS) $(wildcard $(SRC_DIR)/*.h) build/libtrezor.a
	$(CC) $(CFLAGS) -I$(TREZOR_DIR) $(SIGNED_SRCS) build/libtrezor.a -o $@ -lm -Wl,--gc-sections

clean:
	rm -rf build
