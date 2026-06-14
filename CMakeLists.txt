cmake_minimum_required (VERSION 3.10)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("HyperEngine")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # 开启多处理器编译
    add_compile_options(/MP)
endif()

# find_package(Stb REQUIRED)
# target_include_directories(Test PRIVATE ${Stb_INCLUDE_DIR})

# find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
# target_include_directories(Test PRIVATE ${TINYGLTF_INCLUDE_DIRS})

add_definitions(-DASSET_DIR="${CMAKE_SOURCE_DIR}/Asset")
add_definitions(-DSHADER_DIR="${CMAKE_SOURCE_DIR}/Shader")
add_definitions(-DSPIRV_DIR="${CMAKE_SOURCE_DIR}/Shader/Test/SPIRV")
add_definitions(-DRESOURCE_DIR="${CMAKE_SOURCE_DIR}/Resource")

add_subdirectory("ThirdParty")
add_subdirectory("Source/Runtime")
add_subdirectory("Source/Test")
add_subdirectory("Source/Editor")

