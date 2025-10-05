cmake_minimum_required(VERSION 3.10)
project(CJSONServer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/includes)

# Source files
file(GLOB SOURCES "src/*.c")

# Executable
add_executable(server ${SOURCES})

# Libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(MHD REQUIRED libmicrohttpd)
pkg_check_modules(JSONC REQUIRED json-c)
pkg_check_modules(CURL REQUIRED libcurl)

target_include_directories(server PRIVATE ${MHD_INCLUDE_DIRS} ${JSONC_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
target_link_libraries(server ${MHD_LIBRARIES} ${JSONC_LIBRARIES} ${CURL_LIBRARIES})
