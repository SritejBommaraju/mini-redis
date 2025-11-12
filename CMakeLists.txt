cmake_minimum_required(VERSION 3.10)
project(mini_redis)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

include_directories(${PROJECT_SOURCE_DIR}/src)
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(mini_redis ${SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(mini_redis PRIVATE Threads::Threads)
