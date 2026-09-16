cmake_minimum_required(VERSION 3.20)

project(JDrive64 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(jdrive64_core
  src/bam_reader.cpp
  src/d64_reader.cpp
  src/directory_reader.cpp
  src/file_chain_reader.cpp
  src/petscii_converter.cpp
)

target_include_directories(jdrive64_core
  PUBLIC
    include
)

add_executable(jdrive64
  src/main.cpp
)

target_link_libraries(jdrive64
  PRIVATE
    jdrive64_core
)
