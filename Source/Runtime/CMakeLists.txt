option(HYP_SINGLE_THREADED_RENDERING "Use single-threaded rendering (no render thread)" OFF)

include("${CMAKE_SOURCE_DIR}/CMake/AddSmartLibrary.cmake")
include("${CMAKE_SOURCE_DIR}/CMake/CopyLinuxRuntimeDeps.cmake")
include("${CMAKE_SOURCE_DIR}/CMake/GenerateDevConfig.cmake")

include_directories(
    "${CMAKE_SOURCE_DIR}/Source/Runtime"
)

add_subdirectory("Core")
add_subdirectory("Engine")
add_subdirectory("Platform")
add_subdirectory("RHI")
add_subdirectory("Vulkan")
add_subdirectory("WebGPU")
add_subdirectory("RenderBase")
add_subdirectory("Renderer")
add_subdirectory("PassShaders")
add_subdirectory("PyHyperEngine")

if(HYP_SINGLE_THREADED_RENDERING)
    target_compile_definitions(RenderBase PUBLIC HYP_SINGLE_THREADED_RENDERING)
    target_compile_definitions(Engine PUBLIC HYP_SINGLE_THREADED_RENDERING)
endif()

set(RUNTIME_TARGETS Core Engine Platform RHI Vulkan WebGPU RenderBase Renderer PassShaders)
if (TARGET PyHyperEngine)
    list(APPEND RUNTIME_TARGETS PyHyperEngine PyHyperEngineStatic)
endif()
set_property(TARGET ${RUNTIME_TARGETS} PROPERTY FOLDER "Runtime")
