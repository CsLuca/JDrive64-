cmake_minimum_required(VERSION 3.20)

project(JDrive64 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JDRIVE64_ENABLE_WINFSP "Enable WinFsp integration" OFF)

add_library(jdrive64_core
  src/bam_reader.cpp
  src/d64_reader.cpp
  src/disk_catalog.cpp
  src/directory_reader.cpp
  src/file_cache.cpp
  src/file_chain_reader.cpp
  src/petscii_converter.cpp
  src/sector_cache.cpp
  src/winfsp_filesystem.cpp
)

target_include_directories(jdrive64_core
  PUBLIC
    include
)

if(JDRIVE64_ENABLE_WINFSP)
  target_compile_definitions(jdrive64_core PUBLIC JDRIVE64_ENABLE_WINFSP=1)
endif()

add_executable(jdrive64
  src/main.cpp
)

target_link_libraries(jdrive64
  PRIVATE
    jdrive64_core
)

include(CTest)

if(BUILD_TESTING)
  add_executable(jdrive64_tests
    tests/test_core.cpp
  )

  target_link_libraries(jdrive64_tests
    PRIVATE
      jdrive64_core
  )

  add_test(NAME jdrive64_tests COMMAND jdrive64_tests)
endif()
