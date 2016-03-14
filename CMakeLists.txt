cmake_minimum_required(VERSION 2.8)

###################################################################################
project(libgo)

if (CMAKE_BUILD_TYPE)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

message("------------ Options -------------")
message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

aux_source_directory(libgo CO_SRC_LIST)

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
    aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/linux CO_SRC_LIST)
    include_directories(${PROJECT_SOURCE_DIR}/libgo/linux)

    if (ENABLE_BOOST_COROUTINE)
        aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/ctx_boost_coroutine CO_SRC_LIST)
        set(LINK_LIBS "-lboost_coroutine -lboost_context -lboost_thread -lboost_system")
        if (ENABLE_SEGMENTED_STACK)
            set(CXX_FLAGS_POSTFIX "-fsplit-stack -DBOOST_USE_SEGMENTED_STACKS")
            set(TEST_LINK_FLAGS "-fuse-ld=gold")
        endif()

        if (ENABLE_SHARED_STACK)
            message(FATAL_ERROR "ENABLE_SHARED_STACK cannot used with ENABLE_BOOST_COROUTINE")
        endif()
    else ()
        aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/ctx_ucontext CO_SRC_LIST)
        if (ENABLE_SEGMENTED_STACK)
            message(FATAL_ERROR "ENABLE_SEGMENTED_STACK depends ENABLE_BOOST_COROUTINE")
        endif()
    endif()

    if (DISABLE_HOOK)
        list(REMOVE_ITEM CO_SRC_LIST ${PROJECT_SOURCE_DIR}/libgo/linux/linux_glibc_hook.cpp)
        aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/disable_hook CO_SRC_LIST)
    endif()

elseif (WIN32)
    # windows platform
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    include_directories(${PROJECT_SOURCE_DIR}/libgo/windows)
    aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/windows CO_SRC_LIST)
    aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/windows/arpa CO_SRC_LIST)
    aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/windows/sys CO_SRC_LIST)

    if (ENABLE_BOOST_COROUTINE)
        aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/ctx_boost_coroutine CO_SRC_LIST)
        message(WARNING "boost.coroutine was unstable in windows platform")
    else()
        aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/ctx_win_fiber CO_SRC_LIST)
    endif()

    if (ENABLE_SHARED_STACK)
        message(FATAL_ERROR "ENABLE_SHARED_STACK depends unix like platforms")
    endif()

    if (ENABLE_SEGMENTED_STACK)
        message(FATAL_ERROR "ENABLE_SEGMENTED_STACK depends unix like platforms")
    endif()

    if (DISABLE_HOOK)
        list(REMOVE_ITEM CO_SRC_LIST ${PROJECT_SOURCE_DIR}/libgo/windows/win_vc_hook.cpp)
        aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/disable_hook CO_SRC_LIST)
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
    set(CMAKE_CXX_FLAGS_DEBUG "-g -pg ${CMAKE_CXX_FLAGS} -Werror")
    set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 ${CMAKE_CXX_FLAGS} -Werror")
else ()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
endif()
message("------------ Cxx flags -------------")
message("  CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}: ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")
message("------------------------------------")

include_directories(libgo)

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

    add_subdirectory(${PROJECT_SOURCE_DIR}/libgo/main)

    set(CMAKE_INSTALL_PREFIX "/usr/local")
    install(TARGETS ${STATIC_T} ${SHARED_T} LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/libgo/ DESTINATION "include/libgo"
        FILES_MATCHING
        PATTERN "linux" EXCLUDE
        PATTERN "main" EXCLUDE
        PATTERN "windows" EXCLUDE
        PATTERN "ctx_boost_coroutine" EXCLUDE
        PATTERN "ctx_ucontext" EXCLUDE
        PATTERN "*.h")
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/libgo/linux/ DESTINATION "include/libgo" FILES_MATCHING PATTERN "*.h")
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

    set(PROFILE_FLAGS "-g -pg -O3 -std=c++11 -Wall -fPIC")
    #set(PROFILE_FLAGS "-g -pg ${CMAKE_CXX_FLAGS_RELEASE}")
    #message("PROFILE_FLAGS: ${PROFILE_FLAGS}")
    add_custom_target(profile
        COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=PROFILE -DCMAKE_CXX_FLAGS_PROFILE=\\'${PROFILE_FLAGS}\\' ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Switch CMAKE_BUILD_TYPE to PROFILE"
    )

    add_custom_target(uninstall
        COMMAND rm ${CMAKE_INSTALL_PREFIX}/lib/liblibgo.a ${CMAKE_INSTALL_PREFIX}/lib/liblibgo.so ${CMAKE_INSTALL_PREFIX}/lib/liblibgo_main.a -f
        COMMAND rm ${CMAKE_INSTALL_PREFIX}/include/libgo -rf
    )

    add_custom_target(test_all
        COMMAND ${CMAKE_COMMAND} -DOPEN_SUBDIRECTORY_TEST_ALL=ON -DOPEN_SUBDIRECTORY_TEST_DYNAMIC=OFF -DSMALL_TEST=OFF -DTEST_STATIC_LINK=ON -DTEST_LINK_FLAGS="${TEST_LINK_FLAGS}" ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Add subdirectory test_all"
    )
    option(OPEN_SUBDIRECTORY_TEST_ALL "" OFF)
    if (OPEN_SUBDIRECTORY_TEST_ALL)
        add_subdirectory("${PROJECT_SOURCE_DIR}/test/gtest_unit")
    endif()

    add_custom_target(test_dynamic
        COMMAND ${CMAKE_COMMAND} -DOPEN_SUBDIRECTORY_TEST_DYNAMIC=ON -DOPEN_SUBDIRECTORY_TEST_ALL=OFF -DSMALL_TEST=OFF -DTEST_STATIC_LINK=OFF -DTEST_LINK_FLAGS="${TEST_LINK_FLAGS}" ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Add subdirectory test_dynamic"
    )
    option(OPEN_SUBDIRECTORY_TEST_DYNAMIC "" OFF)
    if (OPEN_SUBDIRECTORY_TEST_DYNAMIC)
        add_subdirectory("${PROJECT_SOURCE_DIR}/test/gtest_unit")
    endif()

    add_custom_target(test_small
        COMMAND ${CMAKE_COMMAND} -DOPEN_SUBDIRECTORY_TEST_DYNAMIC=OFF -DOPEN_SUBDIRECTORY_TEST_ALL=OFF -DSMALL_TEST=ON -DTEST_STATIC_LINK=OFF -DTEST_LINK_FLAGS="${TEST_LINK_FLAGS}" ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Add subdirectory test_small"
    )
    option(SMALL_TEST "" OFF)
    if (SMALL_TEST)
        add_subdirectory("${PROJECT_SOURCE_DIR}/test/gtest_unit")
    endif()

    add_custom_target(bm
        COMMAND ${CMAKE_COMMAND} -DOPEN_SUBDIRECTORY_BENCHMARK=ON ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Add subdirectory benchmark"
    )
    option(OPEN_SUBDIRECTORY_BENCHMARK "" OFF)
    if (OPEN_SUBDIRECTORY_BENCHMARK)
        add_subdirectory("${PROJECT_SOURCE_DIR}/test/benchmark")
    endif()

    add_custom_target(samples
        COMMAND ${CMAKE_COMMAND} -DOPEN_SUBDIRECTORY_SAMPLES=ON ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Add subdirectory samples"
    )
    option(OPEN_SUBDIRECTORY_SAMPLES "" OFF)
    if (OPEN_SUBDIRECTORY_SAMPLES)
        add_subdirectory("${PROJECT_SOURCE_DIR}/samples")
    endif()

    add_custom_target(run_test
        COMMAND cp ${PROJECT_SOURCE_DIR}/test/gtest_unit/run_test . && ./run_test
    )

else ()
    add_subdirectory(${PROJECT_SOURCE_DIR}/libgo/main)
    set_target_properties("${STATIC_T}" PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267")

    if (BOOST_ROOT)
        add_subdirectory("${PROJECT_SOURCE_DIR}/test/gtest_unit")
        add_subdirectory("${PROJECT_SOURCE_DIR}/test/benchmark")
        add_subdirectory(${PROJECT_SOURCE_DIR}/samples)
    endif()
endif()

