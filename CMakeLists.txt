cmake_minimum_required(VERSION 3.22)
project(audiovis VERSION 0.1.0 LANGUAGES CXX)

# ── C++20, no extensions ─────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # For clang-tidy

# ── Find system dependencies ─────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(GLFW     REQUIRED glfw3)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# ── Core library (testable without a window) ─────────────────────
add_library(audiovis_core STATIC
    src/audio/capture.cpp
    src/dsp/fft.cpp
    src/dsp/analyser.cpp
)

target_include_directories(audiovis_core
    PUBLIC  src/
            ${PIPEWIRE_INCLUDE_DIRS}
)

target_compile_options(audiovis_core PRIVATE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Woverloaded-virtual -Wconversion -Wsign-conversion
    $<$<CONFIG:Debug>:   -O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer>
    $<$<CONFIG:Release>: -O3 -march=native -ffast-math -flto>
    $<$<CONFIG:Profile>: -O2 -g -fno-inline -fno-omit-frame-pointer>
)

target_link_options(audiovis_core PUBLIC
    $<$<CONFIG:Debug>: -fsanitize=address,undefined>
    $<$<CONFIG:Release>: -flto>
)

target_link_libraries(audiovis_core PUBLIC
    ${PIPEWIRE_LIBRARIES}
)

# ── Main executable ──────────────────────────────────────────────
add_executable(audiovis
    src/main.cpp
    src/render/renderer.cpp
    src/render/shader.cpp
    src/render/visualiser.cpp
)

target_include_directories(audiovis PRIVATE
    src/
    ${GLFW_INCLUDE_DIRS}
)

target_compile_options(audiovis PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:   -O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer>
    $<$<CONFIG:Release>: -O3 -march=native -ffast-math -flto>
    $<$<CONFIG:Profile>: -O2 -g -fno-inline -fno-omit-frame-pointer>
)

target_link_libraries(audiovis PRIVATE
    audiovis_core
    OpenGL::GL
    ${GLFW_LIBRARIES}
    GLEW::GLEW
)

# Copy shaders next to the binary so relative paths work
add_custom_command(TARGET audiovis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:audiovis>/shaders
)

