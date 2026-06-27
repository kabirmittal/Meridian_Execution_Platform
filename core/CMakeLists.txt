cmake_minimum_required(VERSION 3.20)
project(meridian_core VERSION 1.0.0 LANGUAGES CXX)

# ─── C++20 requirement ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Compiler flags ───────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -O2
    )
endif()

# ─── GoogleTest (FetchContent) ────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# ─── meridian_core library ────────────────────────────────────────────────────
add_library(meridian_core STATIC
    src/engine/event_bus.cpp
    src/engine/pipeline.cpp
    src/metrics/metrics.cpp
    src/runtime/sim_runtime.cpp
    src/runtime/prod_runtime.cpp
)

target_include_directories(meridian_core
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(meridian_core PUBLIC cxx_std_20)

# Threads are required by ProdRuntime (std::jthread)
find_package(Threads REQUIRED)
target_link_libraries(meridian_core PUBLIC Threads::Threads)

# ─── Unit tests ───────────────────────────────────────────────────────────────
enable_testing()

function(add_meridian_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME}
        PRIVATE meridian_core GTest::gtest GTest::gtest_main
    )
    gtest_discover_tests(${NAME})
endfunction()

add_meridian_test(test_event_bus   tests/unit/test_event_bus.cpp)
add_meridian_test(test_sim_runtime tests/unit/test_sim_runtime.cpp)
add_meridian_test(test_end_to_end  tests/integration/test_end_to_end.cpp)

# ─── Examples ─────────────────────────────────────────────────────────────────
option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    add_executable(hello_meridian       examples/hello_meridian.cpp)
    add_executable(anomaly_demo         examples/anomaly_detector_demo.cpp)
    add_executable(multi_pipeline_demo  examples/multi_pipeline_demo.cpp)

    foreach(EXAMPLE hello_meridian anomaly_demo multi_pipeline_demo)
        target_link_libraries(${EXAMPLE} PRIVATE meridian_core)
    endforeach()
endif()

# ─── Install rules ────────────────────────────────────────────────────────────
install(TARGETS meridian_core
    EXPORT  MeridianTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/meridian DESTINATION include)
