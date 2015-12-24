cmake_minimum_required(VERSION 2.8)

###################################################################################
include_directories("${PROJECT_SOURCE_DIR}/..")
include_directories("${PROJECT_SOURCE_DIR}/../linux")

add_library(libgo_main libgo_main.cpp)

set(CMAKE_INSTALL_PREFIX "/usr/local")
install(TARGETS libgo_main LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
