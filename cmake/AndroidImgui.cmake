# imgui built from the pinned source fetched by android/build-deps.sh, with the
# SDL3 + OpenGL ES 3 backends and FreeType text rendering.
find_package(SDL3 CONFIG REQUIRED)
find_package(Freetype REQUIRED)

set(STAR_ANDROID_IMGUI_DIR "${PROJECT_SOURCE_DIR}/../android/.deps/src/imgui" CACHE PATH "imgui source checkout")
if(NOT EXISTS "${STAR_ANDROID_IMGUI_DIR}/imgui.cpp")
  message(FATAL_ERROR "imgui sources not found at ${STAR_ANDROID_IMGUI_DIR}; run android/build-deps.sh")
endif()

add_library(imgui STATIC
    ${STAR_ANDROID_IMGUI_DIR}/imgui.cpp
    ${STAR_ANDROID_IMGUI_DIR}/imgui_demo.cpp
    ${STAR_ANDROID_IMGUI_DIR}/imgui_draw.cpp
    ${STAR_ANDROID_IMGUI_DIR}/imgui_tables.cpp
    ${STAR_ANDROID_IMGUI_DIR}/imgui_widgets.cpp
    ${STAR_ANDROID_IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
    ${STAR_ANDROID_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${STAR_ANDROID_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE IMGUI_IMPL_OPENGL_ES3)
target_include_directories(imgui PUBLIC
    ${STAR_ANDROID_IMGUI_DIR}
    ${STAR_ANDROID_IMGUI_DIR}/backends
    ${STAR_ANDROID_IMGUI_DIR}/misc/freetype)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Freetype::Freetype GLESv3)
set(STAR_EXT_LIBS ${STAR_EXT_LIBS} imgui)
# star_extern (imgui_lua_bindings) does not link STAR_EXT_LIBS, so expose the headers directory-wide too.
include_directories(SYSTEM ${STAR_ANDROID_IMGUI_DIR} ${STAR_ANDROID_IMGUI_DIR}/backends)
