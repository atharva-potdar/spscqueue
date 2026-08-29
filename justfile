init:
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    ln -s build/compile_commands.json compile_commands.json
build:
    cmake --build build
run:
    ./build/spscqueue
clean:
    rm -rf build
    rm compile_commands.json
