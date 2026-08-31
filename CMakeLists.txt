cmake_minimum_required(VERSION 3.16)
project(nyx C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(NYX_SANITIZE "Build with AddressSanitizer/UndefinedBehaviorSanitizer" OFF)
option(NYX_GC_STRESS "Force a GC collection before every allocation" OFF)
option(NYX_GC_LOG "Log every GC decision to stderr" OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(NYX_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

if(NYX_GC_STRESS)
  add_compile_definitions(NYX_STRESS_GC)
endif()

if(NYX_GC_LOG)
  add_compile_definitions(NYX_LOG_GC)
endif()

include_directories(include)

file(GLOB_RECURSE NYX_LIB_SOURCES CONFIGURE_DEPENDS "src/*.c")
list(FILTER NYX_LIB_SOURCES EXCLUDE REGEX ".*/main\\.c$")

add_library(nyxcore STATIC ${NYX_LIB_SOURCES})
target_include_directories(nyxcore PUBLIC include)
if(NOT MSVC)
  target_link_libraries(nyxcore PUBLIC m)
endif()

add_executable(nyx src/main.c)
target_link_libraries(nyx PRIVATE nyxcore)

enable_testing()
add_subdirectory(tests)
