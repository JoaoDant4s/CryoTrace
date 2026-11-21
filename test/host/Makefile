SRC_DIR := ../../main
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O1 -I$(SRC_DIR)
SRCS    := test_main.c $(SRC_DIR)/filter.c $(SRC_DIR)/fsm.c $(SRC_DIR)/sensor_sim.c

.PHONY: test clean
test: build/test_main
	./build/test_main

build/test_main: $(SRCS) $(wildcard $(SRC_DIR)/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lm

clean:
	rm -rf build
