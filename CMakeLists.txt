cmake_minimum_required(VERSION 3.16)
project(parsergui VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PARSERGUI_BUILD_GUI "Build the Dear ImGui desktop application" ON)
if(MSVC)
  # Sources and generated reports contain UTF-8 French text and UI strings.
  add_compile_options(/utf-8)
endif()
option(PARSERGUI_BUILD_TESTS "Build the test suite" ON)
option(PARSERGUI_ANDROID "Build the Android native library" OFF)
if(PARSERGUI_ANDROID)
  set(PARSERGUI_BUILD_GUI OFF CACHE BOOL "" FORCE)
  set(PARSERGUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

# No external codec: stb_image / stb_image_write are vendored and self-contained.
if(WIN32)
  set(PGR_PLATFORM_LIBS ws2_32)
elseif(ANDROID)
  set(PGR_PLATFORM_LIBS log)
elseif(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  set(PGR_PLATFORM_LIBS Threads::Threads)
else()
  set(PGR_PLATFORM_LIBS "")
endif()

# ---------------------------------------------------------------------------
# core: portable, no GUI dependency, statically linked into every target
# ---------------------------------------------------------------------------
add_library(pgr_core STATIC
  core/types.cpp
  core/image.cpp
  core/stb_impl.cpp
  core/color.cpp
  core/raster.cpp
  core/segment.cpp
  core/font.cpp
  core/detect.cpp
  core/gui_region.cpp
  core/render.cpp
  core/emit.cpp
  core/json.cpp
  core/parser.cpp
)
target_include_directories(pgr_core PUBLIC core third_party)
if(MSVC)
  target_compile_options(pgr_core PRIVATE /W3 /utf-8)
else()
  target_compile_options(pgr_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# CLI + server
# ---------------------------------------------------------------------------
if(NOT PARSERGUI_ANDROID)
add_executable(parsergui
  cli/main.cpp
  server/http.cpp
  server/jobs.cpp
  server/api.cpp
  agents/exec.cpp
  agents/bridge.cpp
  agents/providers.cpp
)
target_include_directories(parsergui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/server ${CMAKE_CURRENT_SOURCE_DIR}/agents)
target_link_libraries(parsergui PRIVATE pgr_core ${PGR_PLATFORM_LIBS})
if(WIN32)
  target_compile_definitions(parsergui PRIVATE PGR_WINDOWS=1 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(parsergui_fixture tools/fixture_gen.cpp)
target_link_libraries(parsergui_fixture PRIVATE pgr_core)
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if(PARSERGUI_BUILD_TESTS)
  enable_testing()
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test_artifacts)
  add_executable(parsergui_tests tests/test_parser.cpp)
  target_link_libraries(parsergui_tests PRIVATE pgr_core)
  add_test(NAME parser_roundtrip COMMAND parsergui_tests ${CMAKE_BINARY_DIR}/test_artifacts)
  add_executable(parsergui_debug tools/debug_detect.cpp)
  target_link_libraries(parsergui_debug PRIVATE pgr_core)

  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test_artifacts/complex)
  add_executable(parsergui_icon_tests tests/test_icons.cpp)
  target_link_libraries(parsergui_icon_tests PRIVATE pgr_core)
  add_test(NAME icon_recognition COMMAND parsergui_icon_tests)

  add_executable(parsergui_ui_tests tests/test_ui_complex.cpp)
  target_link_libraries(parsergui_ui_tests PRIVATE pgr_core)
  add_test(NAME complex_ui COMMAND parsergui_ui_tests ${CMAKE_BINARY_DIR}/test_artifacts/complex)

  add_executable(parsergui_overlay_tests tests/test_overlay.cpp)
  target_link_libraries(parsergui_overlay_tests PRIVATE pgr_core)
  add_test(NAME overlay_detection COMMAND parsergui_overlay_tests ${CMAKE_BINARY_DIR}/test_artifacts/overlay)
endif()

# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
find_package(OpenGL QUIET)
if(PARSERGUI_BUILD_GUI AND (WIN32 OR APPLE OR UNIX) AND OpenGL_FOUND)
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)

  add_library(pgr_imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(pgr_imgui PUBLIC third_party/imgui third_party/imgui/backends)
  target_link_libraries(pgr_imgui PUBLIC glfw OpenGL::GL)

  add_executable(parsergui_gui app/main_gui.cpp app/ui.cpp)
  target_include_directories(parsergui_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
  target_link_libraries(parsergui_gui PRIVATE pgr_core pgr_imgui)
  target_include_directories(parsergui_gui PRIVATE app third_party/imgui)
  if(WIN32)
    target_compile_definitions(parsergui_gui PRIVATE PGR_WINDOWS=1 NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
endif()

# ---------------------------------------------------------------------------
# Android: native engine only, loaded by the WebView shell over JNI
# ---------------------------------------------------------------------------
if(PARSERGUI_ANDROID)
  add_library(parsergui_native SHARED android/jni_bridge.cpp)
  target_link_libraries(parsergui_native PRIVATE pgr_core log)
  set_target_properties(parsergui_native PROPERTIES OUTPUT_NAME pgr_core)
endif()

# Install rules reference targets that only exist in a desktop build. Under
# Android (PARSERGUI_ANDROID=ON) those targets are not created, and an
# unconditional install(TARGETS ...) makes CMake configure fail.
if(NOT PARSERGUI_ANDROID)
  set(PGR_INSTALL_TARGETS parsergui parsergui_fixture)
  if(TARGET parsergui_gui)
    list(APPEND PGR_INSTALL_TARGETS parsergui_gui)
  endif()
  install(TARGETS ${PGR_INSTALL_TARGETS} RUNTIME DESTINATION bin)
endif()
