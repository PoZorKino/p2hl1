cmake_minimum_required(VERSION 3.20)
project(hl1mov CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Portal 2 is 32-bit, configure from an x86 developer prompt")
endif()

add_library(hl1mov SHARED
  src/plugin.cpp
  src/cvars.cpp
  src/netprops.cpp
  src/rtti.cpp
  src/movement.cpp
)

target_compile_options(hl1mov PRIVATE /W3 /O2 /GS- /EHsc /fp:precise)
target_compile_definitions(hl1mov PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
set_target_properties(hl1mov PROPERTIES PREFIX "" OUTPUT_NAME "hl1mov")
