cmake_minimum_required(VERSION 2.8)

###################################################################################
project(samples)

if (WIN32)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
endif()

include(${PROJECT_SOURCE_DIR}/../boost.cmake)

include_directories(${PROJECT_SOURCE_DIR}/../src)
if (UNIX)
    include_directories(${PROJECT_SOURCE_DIR}/../src/linux)
    set(depend_libgo libgo_dynamic)
else()
    include_directories(${PROJECT_SOURCE_DIR}/../src/windows)
    set(depend_libgo libgo_static)
endif()
link_directories(${PROJECT_SOURCE_DIR}/../build)

if (UNIX)
    set(LINK_ARGS libgo libgo_main boost_coroutine boost_context boost_system boost_thread pthread dl)
else ()
    set(LINK_ARGS libgo ${Boost_LIBRARIES} ws2_32)
endif()

aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)

add_executable(sample2_yield.t sample2_yield.cpp)
add_dependencies(sample2_yield.t ${depend_libgo} libgo_main)
if (WIN32)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/sample2_yield.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/sample3_echo.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/sample9_curl.cpp)
    list(REMOVE_ITEM SRC_LIST ${PROJECT_SOURCE_DIR}/sample11_await.cpp)
    target_link_libraries(sample2_yield.t ${LINK_ARGS} libgo_main)
else()
    target_link_libraries(sample2_yield.t ${LINK_ARGS} rt)
endif()

foreach(var ${SRC_LIST})
    string(REGEX REPLACE ".*/" "" var ${var})
    string(REGEX REPLACE ".cpp" "" tgt ${var})

    set(target ${tgt}.t)
    add_executable(${target} ${var})
    add_dependencies(${target} ${depend_libgo})
    if (WIN32)
        target_link_libraries(${target} ${LINK_ARGS})
        set_target_properties(${target} PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267")
        set_target_properties(${target} PROPERTIES INSTALL_RPATH ${PROJECT_SOURCE_DIR}/../../build)
    else()
        target_link_libraries(${target} ${LINK_ARGS} rt)
    endif()
endforeach(var)

