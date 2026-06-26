cmake_minimum_required(VERSION 3.16)

project(TinyLogLibrary VERSION 0.1.0 LANGUAGES CXX)

file(GLOB_RECURSE TINY_LOG_SOURCES
    src/*.cpp
    include/tiny_log/src/*.cpp
)

add_library(tiny_log STATIC ${TINY_LOG_SOURCES})

set_target_properties(tiny_log PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_compile_features(tiny_log PUBLIC cxx_std_20)

target_include_directories(tiny_log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(tiny_log PUBLIC
    TINY_LOG_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)
