cmake_minimum_required(VERSION 2.8)

###################################################################################
project(libgo)

enable_language(C ASM)

if (CMAKE_BUILD_TYPE)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
    #set(CMAKE_BUILD_TYPE DEBUG)
endif()

message("------------ Options -------------")
message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message("  CMAKE_COMMAND: ${CMAKE_COMMAND}")

option(ENABLE_DEBUGGER "enable debugger" OFF)
if (ENABLE_DEBUGGER)
    set(ENABLE_DEBUGGER 1)
    message ("  enable_debugger: yes")
else()
    set(ENABLE_DEBUGGER 0)
    message ("  enable_debugger: no")
endif()

option(DISABLE_HOOK "disable hook" OFF)
if (DISABLE_HOOK)
    set(ENABLE_HOOK 0)
    message ("  enable_hook: no")
else()
    set(ENABLE_HOOK 1)
    message ("  enable_hook: yes")
endif()


option(ROUTINE_SYNC "use routine_sync tools" ON)
if (ROUTINE_SYNC)
    set(USE_ROUTINE_SYNC 1)
    message ("  use routine_sync tools: yes")
else()
    set(USE_ROUTINE_SYNC 0)
    message ("  use routine_sync tools: no")
endif()

if (BUILD_DYNAMIC)
    message ("  build dynamic lib: yes")
else()
    message ("  build dynamic lib: no")
endif()

message("-------------- Env ---------------")
message("  CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
message("  CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message("----------------------------------")

configure_file(${PROJECT_SOURCE_DIR}/libgo/common/cmake_config.h.in ${PROJECT_SOURCE_DIR}/libgo/common/cmake_config.h)
message("----------------------------------")

if (UNIX)
    set(CMAKE_CXX_FLAGS "-std=c++11 -D__const__=  -fPIC -fno-strict-aliasing -Wall ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DNDEBUG")

    execute_process(COMMAND "chmod" "+x" "${PROJECT_SOURCE_DIR}/third_party/select_asm.sh")
    execute_process(COMMAND "chmod" "+x" "${PROJECT_SOURCE_DIR}/third_party/boost.context/bootstrap.sh")
    execute_process(COMMAND "chmod" "+x" "${PROJECT_SOURCE_DIR}/third_party/boost.context/tools/build/src/engine/build.sh")
	
    set(CMAKE_ASM_SOURCE_FILE_EXTENSIONS S)
    message("--> select asm source file, please wait about 5 seconds ...")
    execute_process(COMMAND "${PROJECT_SOURCE_DIR}/third_party/select_asm.sh" "${PROJECT_SOURCE_DIR}" "jump" OUTPUT_VARIABLE jump_asm_file)
    execute_process(COMMAND "${PROJECT_SOURCE_DIR}/third_party/select_asm.sh" "${PROJECT_SOURCE_DIR}" "make" OUTPUT_VARIABLE make_asm_file)
elseif (WIN32)
    # windows platform
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /EHsc")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /EHsc")

    #set(CMAKE_ASM_SOURCE_FILE_EXTENSIONS asm)
    #file(COPY ${PROJECT_SOURCE_DIR}/third_party/boost.context/libs/context/src/asm/make_x86_64_ms_pe_masm.asm DESTINATION ${PROJECT_SOURCE_DIR}/libgo/context)
    #file(COPY ${PROJECT_SOURCE_DIR}/third_party/boost.context/libs/context/src/asm/jump_x86_64_ms_pe_masm.asm DESTINATION ${PROJECT_SOURCE_DIR}/libgo/context)
endif()

message("------------ Cxx flags -------------")
message("  CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}: ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")
message("------------------------------------")

include_directories(${PROJECT_SOURCE_DIR})
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo CO_SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/common CO_SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/context CO_SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/task CO_SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/scheduler CO_SRC_LIST)
if (ROUTINE_SYNC)
  aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/routine_sync_libgo CO_SRC_LIST)
else()
  aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/sync CO_SRC_LIST)
endif()
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/timer CO_SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/cls CO_SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/defer CO_SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/pool CO_SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/debug CO_SRC_LIST)

if (NOT DISABLE_HOOK)
    if (UNIX)
        aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/netio/unix CO_SRC_LIST)
    elseif (WIN32)
	include_directories(${PROJECT_SOURCE_DIR}/libgo/netio/windows)
        aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/netio/windows CO_SRC_LIST)
	aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/context/fiber CO_SRC_LIST)
	list(APPEND CO_SRC_LIST ${PROJECT_SOURCE_DIR}/libgo/netio/windows/xhook/xhook.cpp)
    endif()
else()
    aux_source_directory(${PROJECT_SOURCE_DIR}/libgo/netio/disable_hook CO_SRC_LIST)
endif()

set(TARGET "libgo")
set(STATIC_T "libgo_static")
set(STATIC_HOOK "static_hook")

list(APPEND CO_SRC_LIST ${jump_asm_file})
list(APPEND CO_SRC_LIST ${make_asm_file})
add_library("${STATIC_T}" STATIC ${CO_SRC_LIST})
set_target_properties("${STATIC_T}" PROPERTIES OUTPUT_NAME "${TARGET}")

if (UNIX)
    install(TARGETS ${STATIC_T} LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/libgo/ DESTINATION "include/libgo" FILES_MATCHING PATTERN "*.h")
        #PATTERN "windows" EXCLUDE

    add_library("${STATIC_HOOK}" STATIC "${PROJECT_SOURCE_DIR}/libgo/netio/unix/static_hook/static_hook.cpp")

    if (BUILD_DYNAMIC)
        set(SHARED_T "libgo_dynamic")
        add_library("${SHARED_T}" SHARED ${CO_SRC_LIST})
        set_target_properties("${SHARED_T}" PROPERTIES OUTPUT_NAME "${TARGET}")
        target_link_libraries("${SHARED_T}" ${LINK_LIBS} -ldl)
        install(TARGETS ${SHARED_T} LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
    endif()

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

    set(PROFILE_FLAGS "-pg ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")

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

elseif (WIN32)
    set_target_properties("${STATIC_T}" PROPERTIES COMPILE_FLAGS "/wd4819 /wd4267")
endif()

if (WIN32)
    if (BOOST_ROOT)
	    add_subdirectory(${PROJECT_SOURCE_DIR}/test/gtest_unit)
	    add_subdirectory(${PROJECT_SOURCE_DIR}/tutorial)
    endif()
endif()
