cmake_minimum_required(VERSION 3.20)
project(AutoClickerM3Cpp LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "Esta versao C++ requer MSVC (WebView2). Use: cmake -G \"Visual Studio 18 2026\" -A x64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# WebView2 SDK (NuGet)
# ---------------------------------------------------------------------------
set(WEBVIEW2_VERSION "1.0.2903.40")
set(WEBVIEW2_NUGET_URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}")
set(WEBVIEW2_ROOT "${CMAKE_BINARY_DIR}/_deps/webview2")
set(WEBVIEW2_NUPKG "${CMAKE_BINARY_DIR}/_deps/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}.nupkg")

if(NOT EXISTS "${WEBVIEW2_ROOT}/build/native/include/WebView2.h")
  message(STATUS "Baixando Microsoft.Web.WebView2 ${WEBVIEW2_VERSION}...")
  file(DOWNLOAD "${WEBVIEW2_NUGET_URL}" "${WEBVIEW2_NUPKG}" SHOW_PROGRESS TLS_VERIFY ON)
  file(ARCHIVE_EXTRACT INPUT "${WEBVIEW2_NUPKG}" DESTINATION "${WEBVIEW2_ROOT}")
endif()

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_ROOT}/build/native/include")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(WEBVIEW2_LIB_DIR "${WEBVIEW2_ROOT}/build/native/x64")
else()
  set(WEBVIEW2_LIB_DIR "${WEBVIEW2_ROOT}/build/native/x86")
endif()

add_executable(AutoClickerM3Cpp WIN32 main.cpp)

target_include_directories(AutoClickerM3Cpp PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_compile_definitions(AutoClickerM3Cpp PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(AutoClickerM3Cpp PRIVATE /W4 /permissive- /utf-8 /EHsc)

target_link_libraries(AutoClickerM3Cpp PRIVATE
  "${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib"
  user32 gdi32 ole32 oleaut32 uuid shell32 shlwapi version winmm advapi32
)

set_target_properties(AutoClickerM3Cpp PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
  OUTPUT_NAME "AutoClickerM3Cpp"
)

# Copia UI HTML para ao lado do executavel
add_custom_command(TARGET AutoClickerM3Cpp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/ui"
    "$<TARGET_FILE_DIR:AutoClickerM3Cpp>/ui"
  COMMENT "Copiando UI WebView2"
)
