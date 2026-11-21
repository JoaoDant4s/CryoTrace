idf_component_register(SRCS "crypto/bignum.c"
                            "crypto/ecdsa.c"
                            "crypto/hmac.c"
                            "crypto/hmac_drbg.c"
                            "crypto/memzero.c"
                            "crypto/rand.c"
                            "crypto/rfc6979.c"
                            "crypto/secp256k1.c"
                            "crypto/sha2.c"
                            "crypto/sha3.c"
                            "port/random_esp.c"
                    INCLUDE_DIRS "crypto"
                    PRIV_REQUIRES esp_hw_support)

# Third-party code: keep it as upstream, without tripping the project warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-variable)
