project(libserv)

cmake_minimum_required(VERSION 2.6)

if(${WIN32})
include(GenerateExportHeader)
endif(${WIN32})

set(CMAKE_MODULE_PATH ${libserv_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH})

set(CMAKE_INSTALL_PREFIX /usr/)

add_subdirectory(src)

set(CPACK_PACKAGE_NAME "libserv")
set(CPACK_PACKAGE_VENDOR "bsg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "libserv - A cross-platform non-blocking TCP server library")
set(CPACK_PACKAGE_VERSION "0.0.1")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "1")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "libserv")
include(CPack)
