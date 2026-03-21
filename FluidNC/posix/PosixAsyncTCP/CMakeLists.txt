cmake_minimum_required(VERSION 3.10)

project(PosixAsyncTCP)

# Source files
set(SOURCES
    PosixAsyncTCP.cpp
)

# Header files
set(HEADERS
    PosixAsyncTCP.h
)

# Create library
add_library(PosixAsyncTCP STATIC ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(PosixAsyncTCP PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link system libraries
target_link_libraries(PosixAsyncTCP PUBLIC
    pthread
)

# Compiler flags for POSIX
if(UNIX)
    target_compile_options(PosixAsyncTCP PRIVATE
        -Wall
        -Wextra
        -std=c++11
    )
endif()

# Enable position-independent code for shared libraries
set_property(TARGET PosixAsyncTCP PROPERTY POSITION_INDEPENDENT_CODE ON)
