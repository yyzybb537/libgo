cmake_minimum_required(VERSION 2.8)

###################################################################################
project(libgo)

aux_source_directory(src CO_SRC_LIST)
set(LARGE_NUM_CO OFF)

if (UNIX)
    # unix like platforms
    aux_source_directory(src/linux CO_SRC_LIST)

    if (ENABLE_BOOST_COROUTINE)
        aux_source_directory(src/ctx_boost_coroutine CO_SRC_LIST)
        set(LINK_LIBS "-lboost_coroutine -lboost_context -lboost_thread -lboost_system")
        if (ENABLE_SEGMENTED_STACK)
            set(CXX_FLAGS_POSTFIX "-fsplit-stack -DBOOST_USE_SEGMENTED_STACKS")
            set(LINK_FLAGS "-fuse-ld=gold")
            set(LARGE_NUM_CO ON)
        endif()
    else ()
        aux_source_directory(src/ctx_ucontext CO_SRC_LIST)
        set(LARGE_NUM_CO ON)
        if (ENABLE_SEGMENTED_STACK)
            message(FATAL_ERROR "ENABLE_SEGMENTED_STACK depends ENABLE_BOOST_COROUTINE")
        endif()
    endif()

    if (DISABLE_HOOK)
        list(remove_item CO_SRC_LIST linux_glibc_hook.cpp)
    endif()

elseif (WIN32)
    # windows platform
    aux_source_directory(src/windows CO_SRC_LIST)
    aux_source_directory(src/windows/arpa CO_SRC_LIST)
    aux_source_directory(src/windows/sys CO_SRC_LIST)
    aux_source_directory(src/ctx_boost_coroutine CO_SRC_LIST)
    set(LINK_LIBS "-lboost_coroutine -lboost_context -lboost_thread -lboost_system")
    if (ENABLE_SEGMENTED_STACK)
        message(FATAL_ERROR "ENABLE_SEGMENTED_STACK depends unix like platforms")
    endif()

    if (DISABLE_HOOK)
    endif()

else ()
    # other platforms
    message(FATAL_ERROR "Not support current platform!")
endif()

if (UNIX)
    set(CXX_FLAGS "-std=c++11 -Wall -Werror -fPIC ${CXX_FLAGS_POSTFIX}")
    set(CMAKE_CXX_FLAGS "-g ${CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 ${CXX_FLAGS}")
endif()

#message(FATAL_ERROR "${CXX_FLAGS}")

include_directories(src)
include_directories(src/linux)

set(TARGET "libgo")
set(STATIC_T "libgo_static")
set(SHARED_T "libgo_dynamic")

add_library("${STATIC_T}" STATIC ${CO_SRC_LIST})
add_library("${SHARED_T}" SHARED ${CO_SRC_LIST})

set_target_properties("${STATIC_T}" PROPERTIES OUTPUT_NAME "${TARGET}")
set_target_properties("${SHARED_T}" PROPERTIES COMPILE_FLAGS "-DCO_DYNAMIC_LINK")
set_target_properties("${SHARED_T}" PROPERTIES OUTPUT_NAME "${TARGET}")
target_link_libraries("${SHARED_T}" ${LINK_LIBS} -ldl)

set(CMAKE_INSTALL_PREFIX "/usr/local")
install(TARGETS ${STATIC_T} ${SHARED_T} LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
install(DIRECTORY src/ DESTINATION "include/libgo"
    FILES_MATCHING
    PATTERN "linux" EXCLUDE
    PATTERN "main" EXCLUDE
    PATTERN "windows" EXCLUDE
    PATTERN "ctx_boost_coroutine" EXCLUDE
    PATTERN "ctx_ucontext" EXCLUDE
    PATTERN "*.h")
install(DIRECTORY src/linux/ DESTINATION "include/libgo" FILES_MATCHING PATTERN "*.h")
install(FILES tools/libgo.conf DESTINATION "/etc/ld.so.conf.d")

add_subdirectory("src/main")

add_custom_target(test
    COMMAND mkdir -p test && cd test && ${CMAKE_COMMAND} -DCXX_FLAGS="\"${CXX_FLAGS}\"" -DLINK_FLAGS="${LINK_FLAGS}" -DLARGE_NUM_CO=${LARGE_NUM_CO} ${PROJECT_SOURCE_DIR}/test/gtest_unit && make -j4 -C gtest && make -j4
    COMMAND cp ${PROJECT_SOURCE_DIR}/test/gtest_unit/test.t ${PROJECT_BINARY_DIR}
    )

