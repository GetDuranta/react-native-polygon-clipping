cmake_minimum_required(VERSION 3.13)
project(PolygonClipping)

set(CMAKE_VERBOSE_MAKEFILE ON)

add_library(
    polygonclipping
    STATIC
    ../cpp/PolygonClippingImpl.cpp
    ../cpp/core/polyclip.cpp
    ../cpp/core/orient2d.cpp
)

set_target_properties(
    polygonclipping PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# The core relies on exact IEEE-754 double semantics (error-free
# transformations in orient2d, bit-compatibility with the JS reference);
# FMA contraction must stay off.
#
# The clipping core is hot-path geometry code; keep it optimized even in
# Debug builds (the trailing -O2 overrides the build type's -O0). Release
# flags are left untouched.
target_compile_options(polygonclipping PRIVATE -ffp-contract=off $<$<CONFIG:Debug>:-O2>)

target_include_directories(
    polygonclipping
    PUBLIC
    ../cpp
)

target_link_libraries(
    polygonclipping
    jsi
    reactnative
    react_codegen_PolygonClippingSpec
)
