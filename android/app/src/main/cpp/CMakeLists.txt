cmake_minimum_required(VERSION 3.24)

project(shadps4-android)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

add_subdirectory(../../../../.. build_shadps4)

set_target_properties(shadps4 PROPERTIES
    OUTPUT_NAME "shadps4"
)
