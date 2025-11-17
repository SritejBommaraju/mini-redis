cmake_minimum_required(VERSION 3.10)
project(mini_redis)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type defaults to Release if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
    add_compile_options(/W4)
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /DNDEBUG)
    else()
        add_compile_options(/Od /Zi)
        add_compile_definitions(DEBUG_LOGGING)
    endif()
else()
    add_compile_options(-Wall -Wextra -pedantic)
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    else()
        add_compile_options(-g -O0)
        add_compile_definitions(DEBUG_LOGGING)
    endif()
endif()

include_directories(${PROJECT_SOURCE_DIR}/src)
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(mini_redis ${SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(mini_redis PRIVATE Threads::Threads)

# Unit tests target (CPU-side only, no networking)
file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
if (TEST_SOURCES)
    add_executable(mini_redis_tests ${TEST_SOURCES})
    target_link_libraries(mini_redis_tests PRIVATE Threads::Threads)
    # Exclude main.cpp from test executable
    set_target_properties(mini_redis_tests PROPERTIES
        EXCLUDE_FROM_ALL TRUE
    )
endif()

# Load generator target
file(GLOB_RECURSE LOADGEN_SOURCES "bench/*.cpp")
if (LOADGEN_SOURCES)
    add_executable(loadgen ${LOADGEN_SOURCES})
    find_package(Threads REQUIRED)
    target_link_libraries(loadgen PRIVATE Threads::Threads)
    if (WIN32)
        target_link_libraries(loadgen PRIVATE ws2_32)
    endif()
endif()
