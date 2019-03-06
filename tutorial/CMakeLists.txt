cmake_minimum_required(VERSION 2.8)

###################################################################################
project(tutorial)

if (CMAKE_BUILD_TYPE)
else()
    #set(CMAKE_BUILD_TYPE RELEASE)
    set(CMAKE_BUILD_TYPE DEBUG)
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
elseif (WIN32)
    # windows platform
    #add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
    #set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /EHsc")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /EHsc")
endif()

option(SMALL_TEST "run the test with small mem and cpu" OFF)
if (${SMALL_TEST} STREQUAL "ON")
    add_definitions(-DSMALL_TEST)
endif()

include(${PROJECT_SOURCE_DIR}/boost.cmake)

#find_package(CURL REQUIRED)
#target_link_libraries(main PRIVATE ${CURL_LIBRARIES})
#include_directories(main PRIVATE ${CURL_INCLUDE_DIRS})

include_directories(${PROJECT_SOURCE_DIR}/..)
include_directories(${PROJECT_SOURCE_DIR}/../libgo)
link_directories(${PROJECT_SOURCE_DIR}/../build)
link_directories(${PROJECT_BINARY_DIR}/..)

aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)

if (WIN32)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/sample4_echo.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/sample9_curl.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/sample12_listener.cpp)
endif()

foreach(var ${SRC_LIST})
    string(REGEX REPLACE ".*/" "" var ${var})
    string(REGEX REPLACE ".cpp" "" tgt ${var})

    add_executable(${tgt}.t ${var})
    if (WIN32)
        set_target_properties(${tgt}.t PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267 /wd4996")
        set_target_properties(${tgt}.t PROPERTIES INSTALL_RPATH ${PROJECT_SOURCE_DIR}/../../build)
        set(LINK_ARGS libgo ${Boost_LIBRARIES} ws2_32)
    elseif (UNIX)
        set(LINK_ARGS libgo curl ${Boost_LIBRARIES} pthread dl)
        #set(LINK_ARGS libgo ${Boost_LIBRARIES} pthread dl)
    endif()
    target_link_libraries(${tgt}.t ${LINK_ARGS})

    #    add_executable(s_${tgt}.t ${var})
    #    if (WIN32)
    #        set_target_properties(s_${tgt}.t PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267")
    #        set(LINK_ARGS libgo libgo_main ${Boost_LIBRARIES} ws2_32)
    #    elseif (UNIX)
    #        set(LINK_ARGS libgo libgo_main ${Boost_LIBRARIES} -Wl,--whole-archive -lstatic_hook -lc -lpthread -Wl,--no-whole-archive -static)
    #    endif()
    #    target_link_libraries(s_${tgt}.t ${TEST_LINK_FLAGS} ${LINK_ARGS})

endforeach(var)
