
# CMake project file.
# Set everything up before commencing with any builds.

# At the time, Gavin was using version 3.7.
cmake_minimum_required(VERSION 3.9)

project(x264_cbr_test)

message(STATUS "Project:    ${PROJECT_NAME}")
message(STATUS "Platform:   ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM})")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

################################################################
# Find Thread - used implicitly by GTest
find_package(Threads REQUIRED)


include(ExternalProject)

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(/DUNICODE)

    # Options added:
    # 1. Increase warning level
    # 2. Make all warnings into errors.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")

endif()


# Set the location where our binaries will be stored.
# WARN/TODO: Not quite right, because .lib or .a files should
#            not go in the bin folder!
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

link_directories(${CMAKE_BINARY_DIR}/bin)

# Local header files here ONLY
set(TARGET_HPP
    )

# Local source files here
set(TARGET_CPP
    main.cpp
    )

# Define an executable
add_executable(x264_cbr ${TARGET_HPP} ${TARGET_CPP})


target_link_libraries(x264_cbr
    avformat
    avcodec
    avutil
    x264
    pthread
	dl
    )
