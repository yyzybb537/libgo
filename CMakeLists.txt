cmake_minimum_required(VERSION 2.8)

###################################################################################
project(libgo)

if (CMAKE_BUILD_TYPE)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

message("------------ Options -------------")
message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

aux_source_directory(src CO_SRC_LIST)

if (ENABLE_BOOST_COROUTINE)
    include (${PROJECT_SOURCE_DIR}/boost.cmake)
    message ("  ENABLE_BOOST_COROUTINE: ON")
    message ("  BOOST_ROOT: ${BOOST_ROOT}")
else()
    message ("  ENABLE_BOOST_COROUTINE: OFF")
endif()

if (ENABLE_SHARED_STACK)
    add_definitions(-DENABLE_SHARED_STACK)
    message ("  ENABLE_SHARED_STACK: ON")
else()
    message ("  ENABLE_SHARED_STACK: OFF")
endif()

if (DISABLE_HOOK)
    message ("  DISABLE_HOOK: ON")
else()
    message ("  DISABLE_HOOK: OFF")
endif()

message("----------------------------------")

message("-------------- Env ---------------")
message("  CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
message("  CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message("----------------------------------")

# 选项互斥检查
if (UNIX)
    # unix like platforms
    aux_source_directory(${PROJECT_SOURCE_DIR}/src/linux CO_SRC_LIST)
    include_directories(${PROJECT_SOURCE_DIR}/src/linux)

    if (ENABLE_BOOST_COROUTINE)
        aux_source_directory(${PROJECT_SOURCE_DIR}/src/ctx_boost_coroutine CO_SRC_LIST)
        set(LINK_LIBS "-lboost_coroutine -lboost_context -lboost_thread -lboost_system")
        if (ENABLE_SEGMENTED_STACK)
            set(CXX_FLAGS_POSTFIX "-fsplit-stack -DBOOST_USE_SEGMENTED_STACKS")
            set(TEST_LINK_FLAGS "-fuse-ld=gold")
        endif()

        if (ENABLE_SHARED_STACK)
            message(FATAL_ERROR "ENABLE_SHARED_STACK cannot used with ENABLE_BOOST_COROUTINE")
        endif()
    else ()
        aux_source_directory(${PROJECT_SOURCE_DIR}/src/ctx_ucontext CO_SRC_LIST)
        if (ENABLE_SEGMENTED_STACK)
            message(FATAL_ERROR "ENABLE_SEGMENTED_STACK depends ENABLE_BOOST_COROUTINE")
        endif()
    endif()

    if (DISABLE_HOOK)
        list(REMOVE_ITEM CO_SRC_LIST ${PROJECT_SOURCE_DIR}/src/linux/linux_glibc_hook.cpp)
        aux_source_directory(${PROJECT_SOURCE_DIR}/src/disable_hook CO_SRC_LIST)
    endif()

elseif (WIN32)
    # windows platform
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    include_directories(${PROJECT_SOURCE_DIR}/src/windows)
    aux_source_directory(${PROJECT_SOURCE_DIR}/src/windows CO_SRC_LIST)
    aux_source_directory(${PROJECT_SOURCE_DIR}/src/windows/arpa CO_SRC_LIST)
    aux_source_directory(${PROJECT_SOURCE_DIR}/src/windows/sys CO_SRC_LIST)

    if (ENABLE_BOOST_COROUTINE)
        aux_source_directory(${PROJECT_SOURCE_DIR}/src/ctx_boost_coroutine CO_SRC_LIST)
        message(WARNING "boost.coroutine was unstable in windows platform")
    else()
        aux_source_directory(${PROJECT_SOURCE_DIR}/src/ctx_win_fiber CO_SRC_LIST)
    endif()

    if (ENABLE_SHARED_STACK)
        message(FATAL_ERROR "ENABLE_SHARED_STACK depends unix like platforms")
    endif()

    if (ENABLE_SEGMENTED_STACK)
        message(FATAL_ERROR "ENABLE_SEGMENTED_STACK depends unix like platforms")
    endif()

    if (DISABLE_HOOK)
        list(REMOVE_ITEM CO_SRC_LIST ${PROJECT_SOURCE_DIR}/src/windows/win_vc_hook.cpp)
        aux_source_directory(${PROJECT_SOURCE_DIR}/src/disable_hook CO_SRC_LIST)
    else ()
        #aux_source_directory(${PROJECT_SOURCE_DIR}/third_party/mhook/mhook-lib CO_SRC_LIST)
        #include_directories(${PROJECT_SOURCE_DIR}/third_party/mhook/mhook-lib)
        #aux_source_directory(${PROJECT_SOURCE_DIR}/third_party/mhook/disasm-lib CO_SRC_LIST)
        #include_directories(${PROJECT_SOURCE_DIR}/third_party/mhook/disasm-lib)

        list(APPEND CO_SRC_LIST "${PROJECT_SOURCE_DIR}/third_party/xhook/src/xhook.cpp")
        include_directories(${PROJECT_SOURCE_DIR}/third_party/xhook/src)
    endif()

else ()
    # other platforms
    message(FATAL_ERROR "Not support current platform!")
endif()

if (UNIX)
    set(CMAKE_CXX_FLAGS "-std=c++11 -fPIC -Wall ${CXX_FLAGS_POSTFIX}")
    set(CMAKE_CXX_FLAGS_DEBUG "-g ${CMAKE_CXX_FLAGS} -Werror")
    set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 ${CMAKE_CXX_FLAGS} -Werror")
else ()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
endif()

#message(FATAL_ERROR "${CMAKE_CXX_FLAGS}")

include_directories(src)

set(TARGET "libgo")
set(STATIC_T "libgo_static")

add_library("${STATIC_T}" STATIC ${CO_SRC_LIST})
set_target_properties("${STATIC_T}" PROPERTIES OUTPUT_NAME "${TARGET}")

if (UNIX)
    set(SHARED_T "libgo_dynamic")
    add_library("${SHARED_T}" SHARED ${CO_SRC_LIST})
    set_target_properties("${SHARED_T}" PROPERTIES COMPILE_FLAGS "-DCO_DYNAMIC_LINK")
    set_target_properties("${SHARED_T}" PROPERTIES OUTPUT_NAME "${TARGET}")
    target_link_libraries("${SHARED_T}" ${LINK_LIBS} -ldl)

    add_subdirectory(${PROJECT_SOURCE_DIR}/src/main)

    set(CMAKE_INSTALL_PREFIX "/usr/local")
    install(TARGETS ${STATIC_T} ${SHARED_T} LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION "include/libgo"
        FILES_MATCHING
        PATTERN "linux" EXCLUDE
        PATTERN "main" EXCLUDE
        PATTERN "windows" EXCLUDE
        PATTERN "ctx_boost_coroutine" EXCLUDE
        PATTERN "ctx_ucontext" EXCLUDE
        PATTERN "*.h")
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/linux/ DESTINATION "include/libgo" FILES_MATCHING PATTERN "*.h")
    install(FILES ${PROJECT_SOURCE_DIR}/tools/libgo.conf DESTINATION "/etc/ld.so.conf.d")

    add_custom_target(debug
        COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=DEBUG ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Switch CMAKE_BUILD_TYPE to Debug"
    )

    add_custom_target(release
        COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=RELEASE ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Switch CMAKE_BUILD_TYPE to Release"
    )

    add_custom_target(uninstall
        COMMAND rm ${CMAKE_INSTALL_PREFIX}/lib/liblibgo.a ${CMAKE_INSTALL_PREFIX}/lib/liblibgo.so ${CMAKE_INSTALL_PREFIX}/lib/liblibgo_main.a -f
        COMMAND rm ${CMAKE_INSTALL_PREFIX}/include/libgo -rf
    )

    add_custom_target(test
        COMMAND mkdir -p test && cd test && ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" -DCMAKE_CXX_FLAGS="\"${CMAKE_CXX_FLAGS}\"" -DTEST_LINK_FLAGS="${TEST_LINK_FLAGS}" -DTEST_STATIC_LINK=1 ${PROJECT_SOURCE_DIR}/test/gtest_unit && make -j4 -C gtest && make -j4
    )

    add_custom_target(test_dynamic
        COMMAND mkdir -p test && cd test && ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" -DCMAKE_CXX_FLAGS="\"${CMAKE_CXX_FLAGS}\"" -DTEST_LINK_FLAGS="${TEST_LINK_FLAGS}" ${PROJECT_SOURCE_DIR}/test/gtest_unit && make -j4 -C gtest && make -j4
    )

    add_custom_target(test_small
        COMMAND mkdir -p test && cd test && ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" -DCMAKE_CXX_FLAGS="\"${CMAKE_CXX_FLAGS}\"" -DTEST_LINK_FLAGS="${TEST_LINK_FLAGS}" -DSMALL_TEST=ON ${PROJECT_SOURCE_DIR}/test/gtest_unit && make -j4 -C gtest && make -j4
    )

    add_custom_target(bm
        COMMAND mkdir -p benchmark && cd benchmark && ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" -DCMAKE_CXX_FLAGS="\"${CMAKE_CXX_FLAGS}\"" -DTEST_LINK_FLAGS="${TEST_LINK_FLAGS}" ${PROJECT_SOURCE_DIR}/test/benchmark && make -j4
    )

    add_custom_target(run_test
        COMMAND cp ${PROJECT_SOURCE_DIR}/test/gtest_unit/run_test . && ./run_test
    )

else ()
    if (BOOST_ROOT)
        add_subdirectory("${PROJECT_SOURCE_DIR}/test/gtest_unit")
        add_subdirectory("${PROJECT_SOURCE_DIR}/test/benchmark")
    endif()
    add_subdirectory(${PROJECT_SOURCE_DIR}/src/main)
    add_subdirectory(${PROJECT_SOURCE_DIR}/samples)
    set_target_properties("${STATIC_T}" PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267")
endif()

