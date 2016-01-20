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

include_directories(${PROJECT_SOURCE_DIR}/../../src)
include_directories(${PROJECT_SOURCE_DIR}/../../src/linux)
link_directories(${PROJECT_SOURCE_DIR}/../../build)

if (UNIX)
    set(LINK_ARGS libgo dl pthread ${Boost_LIBRARIES} pthread)
    set(STATIC_LINK_ARG -static -static-libgcc -static-libstdc++)
else ()
    set(LINK_ARGS libgo ${Boost_LIBRARIES} ws2_32)
endif()

aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/asio SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/asio_sync SRC_LIST)

message("${SRC_LIST}")

foreach(var ${SRC_LIST})
    string(REGEX REPLACE ".*/" "" tgt ${var})
    string(REGEX REPLACE ".cpp" "" tgt ${tgt})
    #message("${TEST_LINK_FLAGS}")

    add_executable(${tgt}.t ${var})
    target_link_libraries(${tgt}.t ${TEST_LINK_FLAGS} ${LINK_ARGS} ${STATIC_LINK_ARG})
    if (WIN32)
        set_target_properties(${tgt}.t PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267")
        add_dependencies(${tgt}.t libgo_static)
    endif()
endforeach(var)

