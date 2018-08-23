cmake_minimum_required(VERSION 2.8)

###################################################################################
project(bench)

if (CMAKE_BUILD_TYPE)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
    #set(CMAKE_BUILD_TYPE DEBUG)
endif()

message("------------ Options -------------")
message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message("  CMAKE_COMMAND: ${CMAKE_COMMAND}")
message("-------------- Env ---------------")
message("  CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
message("  CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message("----------------------------------")

if (UNIX)
    set(CMAKE_CXX_FLAGS "-std=c++11 -fPIC -Wall -m64 ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "-g ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 ${CMAKE_CXX_FLAGS}")
endif()

include_directories(${PROJECT_SOURCE_DIR}/../..)
include_directories(${PROJECT_SOURCE_DIR}/../../libgo)
link_directories(${PROJECT_SOURCE_DIR}/../../build)

aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)

foreach(var ${SRC_LIST})
    string(REGEX REPLACE ".*/" "" var ${var})
    string(REGEX REPLACE ".cpp" "" tgt ${var})

    add_executable(${tgt}.t ${var})
    set(LINK_ARGS libgo pthread dl)
    target_link_libraries(${tgt}.t ${LINK_ARGS})
endforeach(var)

