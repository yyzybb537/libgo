cmake_minimum_required(VERSION 2.8)

###################################################################################
project(libgo)
set(CXX_FLAGS "-std=c++11 -Wall -Werror -fPIC")
set(CMAKE_CXX_FLAGS "-g ${CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 ${CXX_FLAGS}")

aux_source_directory(src CO_SRC_LIST)
aux_source_directory(src/linux CO_SRC_LIST)

include_directories("/src")
include_directories("/src/linux")

set(TARGET "libgo")
set(STATIC_T "libgo_static")
set(SHARED_T "libgo_dynamic")

add_library("${STATIC_T}" STATIC ${CO_SRC_LIST})
add_library("${SHARED_T}" SHARED ${CO_SRC_LIST})

set_target_properties("${STATIC_T}" PROPERTIES OUTPUT_NAME "${TARGET}")
set_target_properties("${SHARED_T}" PROPERTIES COMPILE_FLAGS "-DCO_DYNAMIC_LINK")
set_target_properties("${SHARED_T}" PROPERTIES OUTPUT_NAME "${TARGET}")

set(CMAKE_INSTALL_PREFIX "/usr/local")
install(TARGETS ${STATIC_T} ${SHARED_T} LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
install(DIRECTORY src/ DESTINATION "include/libgo"
    FILES_MATCHING
    PATTERN "linux" EXCLUDE
    PATTERN "main" EXCLUDE
    PATTERN "windows" EXCLUDE
    PATTERN "*.h")
install(DIRECTORY src/linux/ DESTINATION "include/libgo" FILES_MATCHING PATTERN "*.h")
install(FILES tools/libgo.conf DESTINATION "/etc/ld.so.conf.d")

add_subdirectory("src/main")
