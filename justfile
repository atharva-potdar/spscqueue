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
        -DSPSC_SANITIZER=address,undefined
    cmake --build build/asan
    ctest --test-dir build/asan --output-on-failure

test-tsan:
    cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DSPSC_SANITIZER=thread
    cmake --build build/tsan
    ctest --test-dir build/tsan --output-on-failure

clean:
    rm -rf build
    rm -f compile_commands.json
