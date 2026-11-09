cmake_minimum_required(VERSION 3.24)

project(dusk LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(rtxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/RTXUI.git
  # Newer than v0.1.4, the last release: ElementHandle::ScrollIntoView(),
  # flex items keeping their content's height, and /utf-8 for MSVC.
  GIT_TAG        e158f9cf890b692ec0b18835688ae93f8dcfd36c
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(rtxui)

add_library(dusk_lib STATIC src/scan.cpp src/dusk.cpp)
target_include_directories(dusk_lib PUBLIC src)
target_link_libraries(dusk_lib PUBLIC rtxui)

add_executable(dusk src/main.cpp)
target_link_libraries(dusk PRIVATE dusk_lib)
install(TARGETS dusk)

include(CTest)
if(BUILD_TESTING)
  add_executable(dusk_test test/dusk_test.cpp)
  target_link_libraries(dusk_test PRIVATE dusk_lib)
  add_test(NAME dusk COMMAND dusk_test)
endif()
