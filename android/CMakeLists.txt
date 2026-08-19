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
target_compile_options(polygonclipping PRIVATE -ffp-contract=off)

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
