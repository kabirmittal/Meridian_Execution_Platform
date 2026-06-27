# Root CMakeLists.txt — delegates to the core subdirectory.
# Run:  cmake -B build && cmake --build build
cmake_minimum_required(VERSION 3.20)
project(meridian VERSION 1.0.0)
add_subdirectory(core)
