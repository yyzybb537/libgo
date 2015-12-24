cmake_minimum_required(VERSION 2.8)

###################################################################################
project(unit_test)

#set(CXX_FLAGS "-std=c++11 -Wall -Werror -fPIC ${CXX_FLAGS_POSTFIX}")
set(CMAKE_CXX_FLAGS "-g ${CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 ${CXX_FLAGS}")

set(TARGET "unit_test.t")
include_directories(${PROJECT_SOURCE_DIR}/../../third_party/gtest/include)
include_directories(${PROJECT_SOURCE_DIR}/../../src)
include_directories(${PROJECT_SOURCE_DIR}/../../src/linux)
link_directories(${PROJECT_BINARY_DIR}/gtest)
link_directories(${PROJECT_SOURCE_DIR}/../../build)
add_subdirectory(${PROJECT_SOURCE_DIR}/../../third_party/gtest ${PROJECT_BINARY_DIR}/gtest)

set(LINK_ARGS -llibgo -ldl -lgtest_main -lgtest -lpthread -lboost_coroutine -lboost_context -lboost_thread -lboost_system -lpthread)
set(DYNAMIC_LINK_ARG -Wl,-rpath ${PROJECT_SOURCE_DIR}/../../build)
set(STATIC_LINK_ARG -static -static-libgcc -static-libstdc++)

aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)

foreach(var ${SRC_LIST})
    string(REGEX REPLACE ".*/" "" var ${var})
    string(REGEX REPLACE ".cpp" "" tgt ${var})
    #message("${LINK_FLAGS}")

    add_executable(${tgt}.t ${var})
    target_link_libraries(${tgt}.t ${LINK_FLAGS} ${LINK_ARGS} ${DYNAMIC_LINK_ARG})

    add_executable(s_${tgt}.t ${var})
    target_link_libraries(s_${tgt}.t ${LINK_FLAGS} ${LINK_ARGS} ${STATIC_LINK_ARG})
endforeach(var)
