cmake_minimum_required(VERSION 3.16)
project(AutoClickerM3Cpp LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AutoClickerM3Cpp WIN32
  main.cpp
  app.rc
)

if(MSVC)
  target_compile_definitions(AutoClickerM3Cpp PRIVATE UNICODE _UNICODE NOMINMAX)
  target_compile_options(AutoClickerM3Cpp PRIVATE /W4 /utf-8)
else()
  target_compile_definitions(AutoClickerM3Cpp PRIVATE UNICODE _UNICODE NOMINMAX)
  target_compile_options(AutoClickerM3Cpp PRIVATE -Wall -Wextra -municode)
  target_link_options(AutoClickerM3Cpp PRIVATE -mwindows -municode)
endif()

target_link_libraries(AutoClickerM3Cpp PRIVATE gdiplus gdi32 user32 winmm msimg32)

set_target_properties(AutoClickerM3Cpp PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  OUTPUT_NAME "AutoClickerM3Cpp"
)

set_source_files_properties(app.rc PROPERTIES
  OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app.ico"
)
