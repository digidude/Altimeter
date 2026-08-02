cmake_minimum_required(VERSION 3.16.0)
# PlatformIO injects its own EXTRA_COMPONENT_DIRS entry (pointing at
# src/) via the CMake cache — append to it here rather than overwriting,
# or PlatformIO's own "src" component (and its post-build tooling) stops
# resolving.
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/lib)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Altimeter)
