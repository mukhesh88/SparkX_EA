cmake_minimum_required(VERSION 3.20)
project(SparkXEAEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Optimization flags
if(MSVC)
    add_compile_options(/W3 /O2 /MP /EHsc)
else()
    add_compile_options(-Wall -Wextra -O3 -pthread)
endif()

include(FetchContent)

# ------------------------------------------------------------------------------
# 1. GLFW Dependency (FetchContent)
# ------------------------------------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# ------------------------------------------------------------------------------
# 2. Dear ImGui (FetchContent)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
)
FetchContent_MakeAvailable(imgui)

# Build Dear ImGui static library
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${glfw_SOURCE_DIR}/include
)
target_link_libraries(imgui_lib PUBLIC glfw)

# ------------------------------------------------------------------------------
# 3. OpenGL
# ------------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ------------------------------------------------------------------------------
# 4. ZeroMQ / cppzmq (Optional system find with FetchContent fallback)
# ------------------------------------------------------------------------------
find_package(cppzmq QUIET)
if(NOT cppzmq_FOUND)
    FetchContent_Declare(
        cppzmq
        GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
        GIT_TAG        v4.10.0
    )
    # If libzmq is found on system, enable cppzmq
    find_package(ZeroMQ QUIET)
    if(ZeroMQ_FOUND)
        FetchContent_MakeAvailable(cppzmq)
        add_compile_definitions(HAS_ZMQ)
    endif()
else()
    add_compile_definitions(HAS_ZMQ)
endif()

# ------------------------------------------------------------------------------
# 5. ONNX Runtime C++ API (Headers & CUDA provider linkage)
# ------------------------------------------------------------------------------
set(ONNXRUNTIME_LOCAL_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include")
if(EXISTS "${ONNXRUNTIME_LOCAL_INCLUDE}/onnxruntime_cxx_api.h")
    include_directories(${ONNXRUNTIME_LOCAL_INCLUDE})
    add_compile_definitions(HAS_ONNXRUNTIME)
    message(STATUS "Found local ONNX Runtime headers in ${ONNXRUNTIME_LOCAL_INCLUDE}")
endif()

if(DEFINED ONNXRUNTIME_ROOT_DIR)
    message(STATUS "Using ONNX Runtime SDK from: ${ONNXRUNTIME_ROOT_DIR}")
    include_directories(${ONNXRUNTIME_ROOT_DIR}/include)
    link_directories(${ONNXRUNTIME_ROOT_DIR}/lib)
    add_compile_definitions(HAS_ONNXRUNTIME)
    if(ENABLE_CUDA)
        add_compile_definitions(USE_CUDA)
    endif()
endif()

# ------------------------------------------------------------------------------
# 6. SparkX EA GUI Executable
# ------------------------------------------------------------------------------
set(SPARKX_SOURCES
    src/main.cpp
    src/theme.cpp
    src/modernbert_tokenizer.cpp
    src/onnx_laya.cpp
    src/zmq_listener.cpp
    src/ui_panels.cpp
)

set(SPARKX_HEADERS
    include/app_state.hpp
    include/theme.hpp
    include/modernbert_tokenizer.hpp
    include/onnx_laya.hpp
    include/zmq_listener.hpp
    include/ui_panels.hpp
)

add_executable(SparkX_EA ${SPARKX_SOURCES} ${SPARKX_HEADERS})
set_target_properties(SparkX_EA PROPERTIES OUTPUT_NAME "SparkX_EA")

target_include_directories(SparkX_EA PRIVATE
    include
    ${ONNXRUNTIME_LOCAL_INCLUDE}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${glfw_SOURCE_DIR}/include
)

target_link_libraries(SparkX_EA PRIVATE
    imgui_lib
    glfw
    OpenGL::GL
)

if(TARGET cppzmq)
    target_link_libraries(SparkX_EA PRIVATE cppzmq)
endif()

set(ONNXRUNTIME_LOCAL_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib")
if(EXISTS "${ONNXRUNTIME_LOCAL_LIB}/libonnxruntime.dll.a")
    target_link_directories(SparkX_EA PRIVATE ${ONNXRUNTIME_LOCAL_LIB})
    target_link_libraries(SparkX_EA PRIVATE onnxruntime)
    add_custom_command(TARGET SparkX_EA POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_LOCAL_LIB}/onnxruntime.dll"
        $<TARGET_FILE_DIR:SparkX_EA>
    )
endif()

if(DEFINED ONNXRUNTIME_ROOT_DIR)
    target_link_libraries(SparkX_EA PRIVATE onnxruntime)
endif()

# Windows specific linkages & static standalone flags
if(WIN32)
    target_link_libraries(SparkX_EA PRIVATE opengl32 gdi32 user32 shell32 ws2_32)
    if(MINGW)
        target_link_options(SparkX_EA PRIVATE -mwindows -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
    endif()
endif()

# Copy standalone executable and onnxruntime.dll to workspace root for direct 1-click execution
add_custom_command(TARGET SparkX_EA POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:SparkX_EA>"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SparkX_EA.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:SparkX_EA>"
    "${CMAKE_CURRENT_BINARY_DIR}/SparkX_Ai_Algo.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:SparkX_EA>"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SparkX_Ai_Algo.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ONNXRUNTIME_LOCAL_LIB}/onnxruntime.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/../onnxruntime.dll"
    COMMENT "Deploying standalone SparkX_EA.exe and runtime DLLs to workspace root"
)
