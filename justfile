init:
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    ln -sf build/compile_commands.json compile_commands.json

build:
    cmake --build build

run: build
    ./build/spscqueue

# --- tests ------------------------------------------------------------------
test: build
    ctest --test-dir build --output-on-failure

test-asan:
    cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DSPSC_SANITIZER=address,undefined -DSPSC_BUILD_GBENCH=OFF
    cmake --build build/asan
    ctest --test-dir build/asan --output-on-failure

test-tsan:
    cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DSPSC_SANITIZER=thread -DSPSC_BUILD_GBENCH=OFF
    cmake --build build/tsan
    ctest --test-dir build/tsan --output-on-failure

# --- benchmarks -------------------------------------------------------------
bench: build
    ./build/bench_latency
    ./build/bench_throughput

# Five pinned repetitions; writes raw output plus summary.md.
bench-reps:
    REPS=5 bench/run_reps.sh

clean:
    rm -rf build
    rm -f compile_commands.json
