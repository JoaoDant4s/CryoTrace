SRC := ../main/monitoring
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror -I$(SRC)

.PHONY: all figures clean
all: figures

build/filter_bench: filter_bench.c $(SRC)/filter.c $(SRC)/fsm.c $(SRC)/filter.h $(SRC)/fsm.h
	@mkdir -p build
	$(CC) $(CFLAGS) filter_bench.c $(SRC)/filter.c $(SRC)/fsm.c -o $@ -lm

results/filters.csv results/trace.csv: build/filter_bench
	@mkdir -p results
	./build/filter_bench summary > results/filters.csv
	./build/filter_bench trace > results/trace.csv

figures: results/filters.csv results/trace.csv
	@mkdir -p figures
	python3 filter_comparison.py
	python3 data_reduction.py

clean:
	rm -rf build
