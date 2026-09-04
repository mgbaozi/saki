# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(SAKI_BUILD_PROFILE "dev" CACHE STRING "Saki firmware build profile")
set_property(CACHE SAKI_BUILD_PROFILE PROPERTY STRINGS dev release)

if(SAKI_BUILD_PROFILE STREQUAL "dev")
    set(SAKI_VERSION "0.2.0-dev")
elseif(SAKI_BUILD_PROFILE STREQUAL "release")
    set(SAKI_VERSION "0.2.0")
else()
    message(FATAL_ERROR
        "Unsupported SAKI_BUILD_PROFILE='${SAKI_BUILD_PROFILE}'; use dev or release"
    )
endif()

# Keep every profile self-contained. In particular, never allow the historical
# source-tree sdkconfig to leak into a dev or release build.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/config/sdkconfig.vendor;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/config/sdkconfig.${SAKI_BUILD_PROFILE}"
)
set(PROJECT_VER "${SAKI_VERSION}")

add_compile_options(-fdiagnostics-color=always)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(saki)
