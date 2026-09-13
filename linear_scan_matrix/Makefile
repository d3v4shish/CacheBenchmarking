BUILD_DIR := out
CC := gcc
CXX := g++
CLANGXX := clang++
RUSTC := rustc
GO := go

COMMON_WARNINGS := -Wall -Wextra -Werror
COMMON_FLAGS := -O3 -march=haswell -mtune=haswell -mavx2 -fno-tree-vectorize -fno-tree-slp-vectorize

.PHONY: build clean test run benchmark stride16_language random_language arena_language index_language prefetch_language mlp8_language

build: $(BUILD_DIR)/c $(BUILD_DIR)/cpp $(BUILD_DIR)/cpp_native $(BUILD_DIR)/rust $(BUILD_DIR)/go $(BUILD_DIR)/cpp_unroll $(BUILD_DIR)/rust_unroll

# Four deliberately small equivalents used by the stride-16 language matrix.
# These are not aliases of the broader linear/AVX2 matrix above: each one has
# a visible +16-word loop so the article can show exactly what ran.
stride16_language: $(BUILD_DIR)/stride16_c $(BUILD_DIR)/stride16_cpp $(BUILD_DIR)/stride16_rust $(BUILD_DIR)/stride16_go

# Same four language front ends for the random-direct-load control.  The
# permutation and fill are deliberately identical in all four sources.
random_language: $(BUILD_DIR)/random_c $(BUILD_DIR)/random_cpp $(BUILD_DIR)/random_rust $(BUILD_DIR)/random_go

# The same four language front ends for a strict, one-cache-line pointer
# chase.  Unlike direct random loads, no implementation has an independent
# next-address stream that a compiler can vectorize.
arena_language: $(BUILD_DIR)/arena_c $(BUILD_DIR)/arena_cpp $(BUILD_DIR)/arena_rust $(BUILD_DIR)/arena_go

index_language: $(BUILD_DIR)/index_c $(BUILD_DIR)/index_cpp $(BUILD_DIR)/index_rust $(BUILD_DIR)/index_go

# Four source front ends for the late, one-hop software-prefetch control.
# They retain the random 64-byte arena-node cycle used by pointer chasing.
prefetch_language: $(BUILD_DIR)/prefetch_c $(BUILD_DIR)/prefetch_cpp $(BUILD_DIR)/prefetch_rust $(BUILD_DIR)/prefetch_go

# Eight independent load-to-use chains over the same random 64-byte-node
# cycle.  MLP is latency overlap, so this target deliberately has no SIMD.
mlp8_language: $(BUILD_DIR)/mlp8_c $(BUILD_DIR)/mlp8_cpp $(BUILD_DIR)/mlp8_rust $(BUILD_DIR)/mlp8_go

clangpp: $(BUILD_DIR)/clangpp

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/pmu_scope.o: src/pmu_scope.c src/pmu_scope.h | $(BUILD_DIR)
	$(CC) -std=c17 -O2 $(COMMON_WARNINGS) -c $< -o $@

$(BUILD_DIR)/libscanpmu.a: $(BUILD_DIR)/pmu_scope.o
	ar rcs $@ $<

$(BUILD_DIR)/c: src/linear_scan.c $(BUILD_DIR)/libscanpmu.a
	$(CC) -std=c17 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/cpp: src/linear_scan.c $(BUILD_DIR)/libscanpmu.a
	$(CXX) -x c++ -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< -x none $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/clangpp: src/linear_scan.c $(BUILD_DIR)/libscanpmu.a
	$(CLANGXX) -x c++ -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< -x none $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/cpp_native: src/cpp_native.cpp $(BUILD_DIR)/libscanpmu.a
	$(CXX) -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/rust: src/main.rs $(BUILD_DIR)/libscanpmu.a
	$(RUSTC) --edition=2021 -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -L $(BUILD_DIR) -l static=scanpmu $< -o $@

$(BUILD_DIR)/go: src/main.go $(BUILD_DIR)/libscanpmu.a | $(BUILD_DIR)
	cd src && GOCACHE="$(abspath $(BUILD_DIR))/go-cache" CGO_LDFLAGS="-L$(abspath $(BUILD_DIR)) -lscanpmu" GO111MODULE=off $(GO) build -o "$(abspath $(BUILD_DIR))/go" main.go

$(BUILD_DIR)/stride16_c: src/stride16_c.c $(BUILD_DIR)/libscanpmu.a
	$(CC) -std=c17 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/stride16_cpp: src/stride16_cpp.cpp $(BUILD_DIR)/libscanpmu.a
	$(CXX) -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/stride16_rust: src/stride16_rust.rs $(BUILD_DIR)/libscanpmu.a
	$(RUSTC) --edition=2021 -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -L $(BUILD_DIR) -l static=scanpmu $< -o $@

$(BUILD_DIR)/stride16_go: src/stride16_go.go $(BUILD_DIR)/libscanpmu.a | $(BUILD_DIR)
	cd src && GOCACHE="$(abspath $(BUILD_DIR))/go-cache-stride16" CGO_LDFLAGS="-L$(abspath $(BUILD_DIR)) -lscanpmu" GO111MODULE=off $(GO) build -o "$(abspath $(BUILD_DIR))/stride16_go" stride16_go.go

$(BUILD_DIR)/random_c: src/random_c.c $(BUILD_DIR)/libscanpmu.a
	$(CC) -std=c17 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/random_cpp: src/random_cpp.cpp $(BUILD_DIR)/libscanpmu.a
	$(CXX) -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/random_rust: src/random_rust.rs $(BUILD_DIR)/libscanpmu.a
	$(RUSTC) --edition=2021 -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -L $(BUILD_DIR) -l static=scanpmu $< -o $@

$(BUILD_DIR)/random_go: src/random_go.go $(BUILD_DIR)/libscanpmu.a | $(BUILD_DIR)
	cd src && GOCACHE="$(abspath $(BUILD_DIR))/go-cache-random" CGO_LDFLAGS="-L$(abspath $(BUILD_DIR)) -lscanpmu" GO111MODULE=off $(GO) build -o "$(abspath $(BUILD_DIR))/random_go" random_go.go

$(BUILD_DIR)/arena_c: src/arena_c.c $(BUILD_DIR)/libscanpmu.a
	$(CC) -std=c17 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/arena_cpp: src/arena_cpp.cpp $(BUILD_DIR)/libscanpmu.a
	$(CXX) -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/arena_rust: src/arena_rust.rs $(BUILD_DIR)/libscanpmu.a
	$(RUSTC) --edition=2021 -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -L $(BUILD_DIR) -l static=scanpmu $< -o $@

$(BUILD_DIR)/arena_go: src/arena_go.go $(BUILD_DIR)/libscanpmu.a | $(BUILD_DIR)
	cd src && GOCACHE="$(abspath $(BUILD_DIR))/go-cache-arena" CGO_LDFLAGS="-L$(abspath $(BUILD_DIR)) -lscanpmu" GO111MODULE=off $(GO) build -o "$(abspath $(BUILD_DIR))/arena_go" arena_go.go

$(BUILD_DIR)/index_c: src/index_c.c $(BUILD_DIR)/libscanpmu.a
	$(CC) -std=c17 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@
$(BUILD_DIR)/index_cpp: src/index_cpp.cpp $(BUILD_DIR)/libscanpmu.a
	$(CXX) -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@
$(BUILD_DIR)/index_rust: src/index_rust.rs $(BUILD_DIR)/libscanpmu.a
	$(RUSTC) --edition=2021 -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -L $(BUILD_DIR) -l static=scanpmu $< -o $@
$(BUILD_DIR)/index_go: src/index_go.go $(BUILD_DIR)/libscanpmu.a | $(BUILD_DIR)
	cd src && GOCACHE="$(abspath $(BUILD_DIR))/go-cache-index" CGO_LDFLAGS="-L$(abspath $(BUILD_DIR)) -lscanpmu" GO111MODULE=off $(GO) build -o "$(abspath $(BUILD_DIR))/index_go" index_go.go

$(BUILD_DIR)/prefetch_c: src/prefetch_c.c $(BUILD_DIR)/libscanpmu.a
	$(CC) -std=c17 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@
$(BUILD_DIR)/prefetch_cpp: src/prefetch_cpp.cpp $(BUILD_DIR)/libscanpmu.a
	$(CXX) -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@
$(BUILD_DIR)/prefetch_rust: src/prefetch_rust.rs $(BUILD_DIR)/libscanpmu.a
	$(RUSTC) --edition=2021 -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -L $(BUILD_DIR) -l static=scanpmu $< -o $@
$(BUILD_DIR)/prefetch_go: src/prefetch_go/main.go src/prefetch_go/prefetchasm/prefetch.go src/prefetch_go/prefetchasm/prefetch_amd64.s $(BUILD_DIR)/libscanpmu.a | $(BUILD_DIR)
	cd src/prefetch_go && GOCACHE="$(abspath $(BUILD_DIR))/go-cache-prefetch" CGO_LDFLAGS="-L$(abspath $(BUILD_DIR)) -lscanpmu" GO111MODULE=off $(GO) build -o "$(abspath $(BUILD_DIR))/prefetch_go"

$(BUILD_DIR)/mlp8_c: src/mlp8_c.c $(BUILD_DIR)/libscanpmu.a
	$(CC) -std=c17 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/mlp8_cpp: src/mlp8_cpp.cpp $(BUILD_DIR)/libscanpmu.a
	$(CXX) -std=c++20 $(COMMON_FLAGS) $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/mlp8_rust: src/mlp8_rust.rs $(BUILD_DIR)/libscanpmu.a
	$(RUSTC) --edition=2021 -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -L $(BUILD_DIR) -l static=scanpmu $< -o $@

$(BUILD_DIR)/mlp8_go: src/mlp8_go.go $(BUILD_DIR)/libscanpmu.a | $(BUILD_DIR)
	cd src && GOCACHE="$(abspath $(BUILD_DIR))/go-cache-mlp8" CGO_LDFLAGS="-L$(abspath $(BUILD_DIR)) -lscanpmu" GO111MODULE=off $(GO) build -o "$(abspath $(BUILD_DIR))/mlp8_go" mlp8_go.go

$(BUILD_DIR)/cpp_unroll: src/cpp_unroll.cpp $(BUILD_DIR)/libscanpmu.a
	$(CXX) -std=c++20 $(COMMON_FLAGS) -fno-unroll-loops $(COMMON_WARNINGS) -Isrc $< $(BUILD_DIR)/libscanpmu.a -o $@

$(BUILD_DIR)/rust_unroll: src/rust_unroll.rs $(BUILD_DIR)/libscanpmu.a
	$(RUSTC) --edition=2021 -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -L $(BUILD_DIR) -l static=scanpmu $< -o $@

test: build
	scripts/check_source_contract.sh
	scripts/test_validator.sh

run: build
	@echo "requires perf_event_paranoid <= 0: taskset --cpu-list 4 $(BUILD_DIR)/c int scalar 32768 65536"

benchmark:
	@echo "Run on the prepared DUT: scripts/run_on_haswell_dut.sh /home/d3v/linear-scan-matrix-run-UNIQUE"

clean:
	rm -rf $(BUILD_DIR)
