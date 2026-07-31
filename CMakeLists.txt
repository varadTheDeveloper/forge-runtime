cmake_minimum_required(VERSION 3.20)

project(Forge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(VectorTest
    VectorSmokeTest.cpp
    forge-core/memory/DefaultAllocator.cpp
    forge-core/memory/detail/AllocationBackend.cpp
)

target_include_directories(VectorTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(VectorTest PRIVATE /W4)
endif()