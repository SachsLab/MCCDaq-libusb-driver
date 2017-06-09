cmake_minimum_required(VERSION 2.8)

PROJECT(mccdaq)

# General settings
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
cmake_policy(SET CMP0042 NEW) # ENABLE CMP0042: MACOSX_RPATH is enabled by default.

# Third party packages
# libusb        TODO: Consider using ExternalProject_Add on Windows
set(LIBUSB_ROOT "E:\\SachsLab\\Tools\\Misc\\libusb")
find_package(libusb-1.0 REQUIRED)
# Platform-specific libs
SET(PLATFORM_LIBS)
IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    list(APPEND PLATFORM_LIBS
        ${COREFOUNDATION_FRAMEWORK}
        ${IOKIT_FRAMEWORK})
ENDIF()

# Target executable
add_library(${PROJECT_NAME} SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/mccdevice.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mccdevice.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBUSB_1_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    ${LIBUSB_1_LIBRARIES}
    ${PLATFORM_LIBS}
)
