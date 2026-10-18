# Locate the standalone slangc compiler shipped by the vcpkg shader-slang port.
# Used to precompile PassShader .slang sources to SPIRV/WGSL headers at build time
# (see CompilePassShader.cmake), so shipping builds don't need to link the Slang
# compiler or compile these engine-internal shaders at runtime.
find_program(SLANGC_EXECUTABLE
    NAMES slangc
    PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/shader-slang"
    NO_DEFAULT_PATH
)
if(NOT SLANGC_EXECUTABLE)
    find_program(SLANGC_EXECUTABLE NAMES slangc)
endif()
if(NOT SLANGC_EXECUTABLE)
    message(FATAL_ERROR "slangc executable not found (needed to precompile PassShader sources)")
endif()

set(HYP_SPIRV_VERSION "1.5" CACHE STRING "SPIR-V version used to compile PassShaders")
set_property(CACHE HYP_SPIRV_VERSION PROPERTY STRINGS "1.0" "1.3" "1.5" "1.6")
set(_HYP_VALID_SPIRV_VERSIONS "1.0" "1.3" "1.5" "1.6")
if(NOT HYP_SPIRV_VERSION IN_LIST _HYP_VALID_SPIRV_VERSIONS)
    message(FATAL_ERROR "Unsupported HYP_SPIRV_VERSION '${HYP_SPIRV_VERSION}'. Expected one of: 1.0, 1.3, 1.5, 1.6")
endif()

set(HYP_WGSL_VERSION "latest" CACHE STRING "WGSL version used to compile PassShaders")
set_property(CACHE HYP_WGSL_VERSION PROPERTY STRINGS "latest")
if(NOT HYP_WGSL_VERSION STREQUAL "latest")
    message(FATAL_ERROR "Unsupported HYP_WGSL_VERSION '${HYP_WGSL_VERSION}'. Only 'latest' is currently supported")
endif()

# add_pass_shader(<target> <shaderName> <inputSlangFile> [SPIRV_OPT_LEVEL <0-3>] [DEFINES <NAME=VALUE>...])
# Registers a build-time codegen step that compiles <inputSlangFile> into
# ${CMAKE_BINARY_DIR}/GeneratedShaders/$<CONFIG>/<shaderName>.generated.h and adds it as a
# source of <target>, so it participates in incremental rebuilds like any other file.
# 按 $<CONFIG> 分目录生成:Debug 保留调试符号,Release 不保留(体积更小、和运行时编译流程原有行为一致)
# SPIRV_OPT_LEVEL 可选,不传时用 CompilePassShader.cmake 里的默认值(-O3,和迁移前运行时编译行为一致)。
# 目前只有涉及 wave/subgroup 操作的 shader(如 RadixSort)在怀疑 -O3 触发编译器 bug 时才需要单独调低。
# DEFINES 可选,每一项以 -D NAME=VALUE 传给 slangc,用来从同一份 .slang 在构建期实例化出不同变体。
function(add_pass_shader TARGET_NAME SHADER_NAME INPUT_SLANG)
    cmake_parse_arguments(ARG "" "SPIRV_OPT_LEVEL" "DEFINES" ${ARGN})
    set(OUTPUT_HEADER "${CMAKE_BINARY_DIR}/GeneratedShaders/$<CONFIG>/${SHADER_NAME}.generated.h")
    set(EXTRA_DEFINES "")
    if(DEFINED ARG_SPIRV_OPT_LEVEL)
        list(APPEND EXTRA_DEFINES "-DSPIRV_OPT_LEVEL=${ARG_SPIRV_OPT_LEVEL}")
    endif()
    if(ARG_DEFINES)
        list(JOIN ARG_DEFINES "|" SLANG_DEFINES_JOINED)
        list(APPEND EXTRA_DEFINES "-DSLANG_DEFINES=${SLANG_DEFINES_JOINED}")
    endif()
    add_custom_command(
        OUTPUT "${OUTPUT_HEADER}"
        COMMAND "${CMAKE_COMMAND}"
            "-DSLANGC=${SLANGC_EXECUTABLE}"
            "-DINPUT=${INPUT_SLANG}"
            "-DOUTPUT=${OUTPUT_HEADER}"
            "-DNAME=${SHADER_NAME}"
            "-DSHADER_DIR=${CMAKE_SOURCE_DIR}/Shader"
            "-DDEBUG_SYMBOLS=$<CONFIG:Debug>"
            "-DHYP_SPIRV_VERSION=${HYP_SPIRV_VERSION}"
            "-DHYP_WGSL_VERSION=${HYP_WGSL_VERSION}"
            ${EXTRA_DEFINES}
            -P "${CMAKE_SOURCE_DIR}/CMake/CompilePassShader.cmake"
        DEPENDS "${INPUT_SLANG}" "${CMAKE_SOURCE_DIR}/CMake/CompilePassShader.cmake"
        DEPFILE "${OUTPUT_HEADER}.d"
        COMMENT "Compiling PassShader ${SHADER_NAME} ($<CONFIG>)"
        VERBATIM
    )
    target_sources(${TARGET_NAME} PRIVATE "${OUTPUT_HEADER}")
endfunction()
