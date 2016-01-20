cmake_minimum_required(VERSION 2.8)

###################################################################################
project(unit_test)

if (UNIX)
    set(CMAKE_CXX_FLAGS "-g ${CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 ${CXX_FLAGS}")
else ()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
endif()

include(${PROJECT_SOURCE_DIR}/../../boost.cmake)

set(TARGET "unit_test.t")
include_directories(${PROJECT_SOURCE_DIR}/../../third_party/gtest/include)
include_directories(${PROJECT_SOURCE_DIR}/../../src)
include_directories(${PROJECT_SOURCE_DIR}/../../src/linux)
link_directories(${PROJECT_BINARY_DIR}/gtest)
link_directories(${PROJECT_SOURCE_DIR}/../../build)
add_subdirectory(${PROJECT_SOURCE_DIR}/../../third_party/gtest ${PROJECT_BINARY_DIR}/gtest)

if (UNIX)
    set(LINK_ARGS libgo dl gtest_main gtest pthread ${Boost_LIBRARIES} pthread)
    set(STATIC_LINK_ARG -static -static-libgcc -static-libstdc++)
else ()
    set(LINK_ARGS libgo gtest_main gtest ${Boost_LIBRARIES} ws2_32)
endif()

if (LARGE_NUM_CO)
    add_definitions(-DLARGE_NUM_CO)
endif()

aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)

if (WIN32)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/file.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/io_timed.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/poll.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/sleep.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/select.cpp)
endif()

foreach(var ${SRC_LIST})
    string(REGEX REPLACE ".*/" "" var ${var})
    string(REGEX REPLACE ".cpp" "" tgt ${var})
    #message("${TEST_LINK_FLAGS}")

    if (("${TEST_LINK_FLAGS}" STREQUAL "") AND UNIX)
        add_executable(${tgt}.t ${var})
        target_link_libraries(${tgt}.t ${LINK_ARGS})
        if (WIN32)
            set_target_properties(${tgt}.t PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267")
            set_target_properties(${tgt}.t PROPERTIES INSTALL_RPATH ${PROJECT_SOURCE_DIR}/../../build)
        endif()
    endif()

    add_executable(s_${tgt}.t ${var})
    target_link_libraries(s_${tgt}.t ${TEST_LINK_FLAGS} ${LINK_ARGS} ${STATIC_LINK_ARG})
    if (WIN32)
        set_target_properties(s_${tgt}.t PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267")
        add_dependencies(s_${tgt}.t libgo_static)
    endif()
endforeach(var)

