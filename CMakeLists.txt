cmake_minimum_required(VERSION 2.8)

###################################################################################
project(libgo)

aux_source_directory(src CO_SRC_LIST)
set(LARGE_NUM_CO OFF)

if (ENABLE_BOOST_COROUTINE OR WIN32)
    include (${PROJECT_SOURCE_DIR}/boost.cmake)
endif()

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
            set(LARGE_NUM_CO ON)
        endif()
    else ()
        aux_source_directory(${PROJECT_SOURCE_DIR}/src/ctx_ucontext CO_SRC_LIST)
        set(LARGE_NUM_CO ON)
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
    aux_source_directory(${PROJECT_SOURCE_DIR}/src/windows CO_SRC_LIST)
    aux_source_directory(${PROJECT_SOURCE_DIR}/src/windows/arpa CO_SRC_LIST)
    aux_source_directory(${PROJECT_SOURCE_DIR}/src/windows/sys CO_SRC_LIST)
    aux_source_directory(${PROJECT_SOURCE_DIR}/src/ctx_boost_coroutine CO_SRC_LIST)
    include_directories(${PROJECT_SOURCE_DIR}/src/windows)
    set(LINK_LIBS "-lboost_coroutine -lboost_context -lboost_thread -lboost_system")
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
    set(CXX_FLAGS "-std=c++11 -Wall -Werror -fPIC ${CXX_FLAGS_POSTFIX}")
    set(CMAKE_CXX_FLAGS "-g ${CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 ${CXX_FLAGS}")
else ()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} /MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
endif()

#message(FATAL_ERROR "${CXX_FLAGS}")

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

    add_custom_target(uninstall
        COMMAND rm ${CMAKE_INSTALL_PREFIX}/lib/liblibgo.a ${CMAKE_INSTALL_PREFIX}/lib/liblibgo.so ${CMAKE_INSTALL_PREFIX}/lib/liblibgo_main.a -f
        COMMAND rm ${CMAKE_INSTALL_PREFIX}/include/libgo -rf
    )

    add_custom_target(test
        COMMAND mkdir -p test && cd test && ${CMAKE_COMMAND} -DCXX_FLAGS="\"${CXX_FLAGS}\"" -DTEST_LINK_FLAGS="${TEST_LINK_FLAGS}" -DLARGE_NUM_CO=${LARGE_NUM_CO} ${PROJECT_SOURCE_DIR}/test/gtest_unit && make -j4 -C gtest && make -j4
        COMMAND cp ${PROJECT_SOURCE_DIR}/test/gtest_unit/test.t ${PROJECT_BINARY_DIR}
    )
else ()
    add_subdirectory("${PROJECT_SOURCE_DIR}/test/gtest_unit")
    set_target_properties("${STATIC_T}" PROPERTIES COMPILE_FLAGS "/wd4819")
endif()

