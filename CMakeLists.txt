cmake_minimum_required(VERSION 3.24)
project(spscqueue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SPSC_BUILD_TESTS "Build the unit and stress tests" ON)
option(SPSC_WERROR "Treat compiler warnings as errors" ON)
set(SPSC_SANITIZER "" CACHE STRING
    "Sanitizer to enable: address,undefined | thread | empty for none")

find_package(Threads REQUIRED)

# --- Shared options ---------------------------------------------------------
add_library(spsc_options INTERFACE)
target_include_directories(spsc_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spsc_options INTERFACE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(spsc_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion)
    if(SPSC_WERROR)
        target_compile_options(spsc_options INTERFACE -Werror)
    endif()
endif()

if(SPSC_SANITIZER)
    message(STATUS "Building with -fsanitize=${SPSC_SANITIZER}")
    add_compile_options(-fsanitize=${SPSC_SANITIZER} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${SPSC_SANITIZER})
endif()

# --- Tests ------------------------------------------------------------------
if(SPSC_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS test/test_*.cpp)
    list(REMOVE_ITEM TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/test/test_no_alloc.cpp)

    foreach(src IN LISTS TEST_SOURCES)
        cmake_path(GET src STEM name)
        add_executable(${name} ${src})
        target_link_libraries(${name} PRIVATE spsc_options GTest::gtest_main)
        # Keep assert()-based invariants active in Release too.
        target_compile_options(${name} PRIVATE -UNDEBUG)
        add_test(NAME ${name} COMMAND ${name})
    endforeach()

    add_executable(test_no_alloc test/test_no_alloc.cpp)
    target_link_libraries(test_no_alloc PRIVATE spsc_options)
    add_test(NAME test_no_alloc COMMAND test_no_alloc)
endif()
