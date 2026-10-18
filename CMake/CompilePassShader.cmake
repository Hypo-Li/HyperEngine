# cmake -P script mode: compile a single PassShader .slang file into a generated header
# containing precompiled SPIRV (uint32_t array) and WGSL (C string) bytecode for each
# stage the file actually declares via [shader("...")] attributes.
#
# Required -D arguments:
#   SLANGC        - path to slangc executable
#   INPUT         - path to the .slang source file
#   OUTPUT        - path to the generated .h file to write
#   NAME          - identifier prefix used for the generated array/string names
#   SHADER_DIR    - include search path passed to slangc (-I)
#   DEBUG_SYMBOLS - "1" to keep debug info (-g2, matches Debug config), "0"/unset to strip it (Release)
#   HYP_SPIRV_VERSION - SPIR-V target version (default 1.5)
#   HYP_WGSL_VERSION  - WGSL target version (currently only "latest")
#   SPIRV_OPT_LEVEL - optional, overrides the SPIRV -O level (default: 0 when DEBUG_SYMBOLS=="1",
#                     3 otherwise). 涉及 wave/subgroup 操作、怀疑被优化触发编译器 bug 的 shader
#                     (比如 RadixSort 系列)可以显式传这个参数固定成某个值,不随 DEBUG_SYMBOLS 变
#   SLANG_DEFINES - optional, "|" 分隔的 NAME=VALUE 列表, 每一项以 -D NAME=VALUE 传给 slangc

if(NOT DEFINED SLANGC OR NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED NAME OR NOT DEFINED SHADER_DIR)
    message(FATAL_ERROR "CompilePassShader.cmake requires SLANGC, INPUT, OUTPUT, NAME, SHADER_DIR")
endif()

set(DEBUG_INFO_FLAGS "")
if(DEBUG_SYMBOLS STREQUAL "1")
    set(DEBUG_INFO_FLAGS "-g2")
endif()

if(NOT DEFINED SPIRV_OPT_LEVEL)
    if(DEBUG_SYMBOLS STREQUAL "1")
        set(SPIRV_OPT_LEVEL "0")
    else()
        set(SPIRV_OPT_LEVEL "3")
    endif()
endif()

if(NOT DEFINED HYP_SPIRV_VERSION)
    set(HYP_SPIRV_VERSION "1.5")
endif()
set(_HYP_VALID_SPIRV_VERSIONS "1.0" "1.3" "1.5" "1.6")
if(NOT HYP_SPIRV_VERSION IN_LIST _HYP_VALID_SPIRV_VERSIONS)
    message(FATAL_ERROR "Unsupported HYP_SPIRV_VERSION '${HYP_SPIRV_VERSION}'. Expected one of: 1.0, 1.3, 1.5, 1.6")
endif()
string(REPLACE "." "_" SPIRV_PROFILE_SUFFIX "${HYP_SPIRV_VERSION}")
set(SPIRV_PROFILE "spirv_${SPIRV_PROFILE_SUFFIX}")

if(NOT DEFINED HYP_WGSL_VERSION)
    set(HYP_WGSL_VERSION "latest")
endif()
if(NOT HYP_WGSL_VERSION STREQUAL "latest")
    message(FATAL_ERROR "Unsupported HYP_WGSL_VERSION '${HYP_WGSL_VERSION}'. Only 'latest' is currently supported")
endif()

set(SLANG_DEFINE_ARGS "")
if(DEFINED SLANG_DEFINES)
    string(REPLACE "|" ";" SLANG_DEFINE_LIST "${SLANG_DEFINES}")
    foreach(SLANG_DEFINE ${SLANG_DEFINE_LIST})
        list(APPEND SLANG_DEFINE_ARGS -D "${SLANG_DEFINE}")
    endforeach()
endif()

file(READ "${INPUT}" SOURCE_TEXT)

# 递归展开本地 #include "..."(相对当前文件目录解析, 跟 slangc 对引号 include 的语义一致),
# 拼出跟 SOURCE_TEXT 等价但包含所有间接依赖内容的一份文本, 供下面 PUSH_CONSTANT_NAME 检测使用——
# entry 文件(比如 SSAOHalfRes.slang)本身可能不含字面的 [[vk::push_constant]], 声明实际
# 写在它 #include 的共享文件(SSAOCommon.slang)里, 只扫 SOURCE_TEXT 会检测不到
function(hyp_gather_include_text FILE_PATH SEEN_LIST OUT_VAR)
    if("${FILE_PATH}" IN_LIST SEEN_LIST)
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()
    list(APPEND SEEN_LIST "${FILE_PATH}")

    if(NOT EXISTS "${FILE_PATH}")
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()
    file(READ "${FILE_PATH}" FILE_TEXT)
    set(COMBINED_TEXT "${FILE_TEXT}")

    get_filename_component(FILE_DIR "${FILE_PATH}" DIRECTORY)
    string(REGEX MATCHALL "#include[ \t]+\"[^\"]+\"" INCLUDE_LINES "${FILE_TEXT}")
    foreach(INCLUDE_LINE ${INCLUDE_LINES})
        string(REGEX REPLACE "#include[ \t]+\"([^\"]+)\"" "\\1" INCLUDE_REL_PATH "${INCLUDE_LINE}")
        get_filename_component(INCLUDE_ABS_PATH "${FILE_DIR}/${INCLUDE_REL_PATH}" ABSOLUTE)
        hyp_gather_include_text("${INCLUDE_ABS_PATH}" "${SEEN_LIST}" NESTED_TEXT)
        string(APPEND COMBINED_TEXT "\n${NESTED_TEXT}")
    endforeach()

    set(${OUT_VAR} "${COMBINED_TEXT}" PARENT_SCOPE)
endfunction()

hyp_gather_include_text("${INPUT}" "" SOURCE_TEXT_WITH_INCLUDES)

# Slang's WGSL backend lowers [format("...")] storage textures of several formats to
# texture_storage_*<rgba32float, ...>, even though SPIR-V reflection correctly reports the real
# storage format. Dawn then rejects the pipeline because the shader format disagrees with the
# bind-group layout. `half` is not a workaround: WGSL texture element types are restricted to
# f32/i32/u32 and Slang rejects WTexture2D<half>.
#
# Patch only reflected storage bindings of the affected formats, identified by their exact
# group, binding index and source resource name. A multi-stage shader may expose the binding in
# only one stage, so absence from the current stage is valid; when present, verify the rewritten
# declaration before embedding the WGSL.
#   CONTENT_VAR - name of the variable holding the WGSL text; rewritten in the caller's scope
#   GROUP       - bind group index the INDICES were collected from
#   PREFIX      - "BINDING_" / "BINDING_S0_", the accumulated-table variable prefix for INDICES
function(hyp_patch_wgsl_storage_formats CONTENT_VAR GROUP PREFIX INDICES INPUT_FILE STAGE_NAME)
    set(CONTENT "${${CONTENT_VAR}}")
    foreach(IDX IN LISTS INDICES)
        if(NOT "${${PREFIX}${IDX}_TYPE}" STREQUAL "Storage_Texture")
            continue()
        endif()

        set(FORMAT "${${PREFIX}${IDX}_STORAGEFORMAT}")
        set(WGSL_FORMAT "")
        if(FORMAT STREQUAL "R16_SFLOAT")
            set(WGSL_FORMAT "r16float")
        elseif(FORMAT STREQUAL "R16_UNORM")
            set(WGSL_FORMAT "r16unorm")
        elseif(FORMAT STREQUAL "R8_UNORM")
            set(WGSL_FORMAT "r8unorm")
        elseif(FORMAT STREQUAL "RG8_UNORM")
            set(WGSL_FORMAT "rg8unorm")
        elseif(FORMAT STREQUAL "R8_UINT")
            set(WGSL_FORMAT "r8uint")
        elseif(FORMAT STREQUAL "B10G11R11_UFLOAT_PACK32")
            set(WGSL_FORMAT "rg11b10ufloat")
        elseif(FORMAT STREQUAL "A2BGR10_UNORM_PACK32")
            set(WGSL_FORMAT "rgb10a2unorm")
        endif()
        if(WGSL_FORMAT STREQUAL "")
            continue()
        endif()

        set(BINDING_NAME "${${PREFIX}${IDX}_NAME}")
        set(DECL_PATTERN
            "(@binding\\(${IDX}\\) @group\\(${GROUP}\\) var ${BINDING_NAME}_[0-9]+ : texture_storage_[^<]+)<[^,>]+,")
        string(REGEX MATCH "${DECL_PATTERN}" DECL_MATCH "${CONTENT}")
        if(DECL_MATCH)
            string(REGEX REPLACE "${DECL_PATTERN}" "\\1<${WGSL_FORMAT}," CONTENT "${CONTENT}")

            set(VERIFY_PATTERN
                "@binding\\(${IDX}\\) @group\\(${GROUP}\\) var ${BINDING_NAME}_[0-9]+ : texture_storage_[^<]+<${WGSL_FORMAT},")
            string(REGEX MATCH "${VERIFY_PATTERN}" VERIFY_MATCH "${CONTENT}")
            if(NOT VERIFY_MATCH)
                message(FATAL_ERROR
                    "Failed to patch WGSL ${FORMAT} storage binding '${BINDING_NAME}' at group ${GROUP} binding ${IDX} in ${INPUT_FILE} (${STAGE_NAME})")
            endif()
        endif()
    endforeach()
    set(${CONTENT_VAR} "${CONTENT}" PARENT_SCOPE)
endfunction()

set(STAGE_NAMES vertex fragment compute)
set(STAGE_ENTRY_vertex "VertexMain")
set(STAGE_ENTRY_fragment "FragmentMain")
set(STAGE_ENTRY_compute "ComputeMain")
set(STAGE_LABEL_vertex "Vertex")
set(STAGE_LABEL_fragment "Fragment")
set(STAGE_LABEL_compute "Compute")

get_filename_component(TMP_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${TMP_DIR}")

# Slang doesn't emit WGSL's dedicated `immediate` address space for [[vk::push_constant]] yet
# (as of this vendored Slang version) - it falls back to a plain, unannotated `var<uniform>`.
# Detect the push-constant field name so the WGSL branch below can patch that one declaration
# into `var<immediate>` (the real WGSL syntax, see gpuweb PR #5423) until Slang supports this
# natively. This is a targeted, name-matched substitution, not a blind text replace.
# 引擎里所有 PassShader 一律用 [[vk::push_constant]](和资源绑定的 [[vk::binding(...)]] 保持
# 同一套 vk:: 前缀写法), 不用不带前缀的 [[push_constant]]——这个正则只认前者
set(PUSH_CONSTANT_NAME "")
string(REGEX MATCH "\\[\\[vk::push_constant\\]\\][ \t\r\n]*[A-Za-z_][A-Za-z0-9_]*[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;" PC_MATCH "${SOURCE_TEXT_WITH_INCLUDES}")
if(CMAKE_MATCH_1)
    set(PUSH_CONSTANT_NAME "${CMAKE_MATCH_1}")
endif()

set(GENERATED_CONTENT "#pragma once\n#include <cstdint>\n#include <RHI/BindGroup.h>\n\nnamespace hyp::generated\n{\n")

# 累积每次 slangc 调用(每个 stage x SPIRV/WGSL)各自吐出的 depfile 里的依赖文件列表,
# 最后合并成一份,写给 CMake 的 DEPFILE 用,让 #include/import 的间接依赖也能正确触发增量重编。
# 依赖列表本身(冒号右边那部分)在不同 target/宏组合下不一定完全一样(比如条件 include),
# 所以每次调用都要单独抓一次,不能只抓一次复用;重复列出同一个依赖是无害的,不需要去重。
set(ALL_DEPS "")

# pass 级 bind group(见 RenderPassUtils.h::createRenderPassPipelineLayout: set 0 = 全局,
# set 1 = scene/view, set 2 = pass/material)的 binding 描述, 从 slangc -reflection-json 解析
# 得来, 跨 vertex/fragment/compute 多个 stage 累积——同一个 binding index 在多个 stage 里
# 出现时只登记一次, stageFlags 取并集。用 "BINDING_<index>_*" 这种动态变量名模拟按 index
# 查找的字典, ALL_BINDING_INDICES 记录出现过的 index、保证后面能按顺序遍历。
set(ALL_BINDING_INDICES "")

# 纯工具/计算 shader(比如 RadixSort 系列)完全不 #include "../Common/Binding.slang", 也就没有
# set 2——它们把自己仅有的几个资源直接声明在 set 0, C++ 侧配的也是只含这一个 BindGroupLayout 的
# 精简 PipelineLayout(不是标准的 [全局, scene/view, pass] 三段式)。这里用同样的动态变量字典,
# 加一层 "S0_" 前缀单独收集 set 0 的绑定, 避免和 set 2 的收集结果混在一起。是否真的采用这份
# set 0 收集结果, 要等所有 stage 都扫完、确认 set 2 完全没有任何绑定之后才能决定(见文件末尾),
# 不能在这里提前判断——单个 stage 里 set 2 为空不代表整个 shader 都没有 set 2(不同 stage
# 各自只用到 set 2 里的一部分资源, 是常见情况)。
set(ALL_BINDING_INDICES_S0 "")

foreach(STAGE ${STAGE_NAMES})
    string(FIND "${SOURCE_TEXT}" "[shader(\"${STAGE}\")]" STAGE_FOUND)
    if(STAGE_FOUND EQUAL -1)
        continue()
    endif()

    set(ENTRY "${STAGE_ENTRY_${STAGE}}")
    set(LABEL "${STAGE_LABEL_${STAGE}}")

    # ---- SPIRV: slangc emits a ready-to-include const uint32_t array directly ----
    # (slangc appends its own .h extension unless the given -o path already ends in .h)
    set(SPIRV_HEADER "${TMP_DIR}/${NAME}_${LABEL}.spirv.tmp.h")
    set(SPIRV_DEPFILE "${TMP_DIR}/${NAME}_${LABEL}.spirv.d")
    set(STAGE_REFLECTION_JSON "${TMP_DIR}/${NAME}_${LABEL}.reflect.json")
    # Release 默认 -O3 而不是 slangc 自己的默认 -O1, 是因为 -O1 生成的指令序列不同,
    # 对大气积分这类浮点敏感代码会产生可见的数值偏差。
    # -matrix-layout-row-major: 迁移前运行时编译显式设置了 sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR,
    # slangc CLI 不指定时默认是 column-major——ViewData/InstanceData 等 buffer 里的矩阵(worldToViewRot/viewToClip 等)
    # 会被按错误的主序读取,导致 view/相机相关的计算全错(大气 LUT 颜色偏差的真正根因)
    execute_process(
        COMMAND "${SLANGC}" "${INPUT}"
            -entry "${ENTRY}" -stage "${STAGE}"
            -target spirv -profile "${SPIRV_PROFILE}"
            -D RHI_BACKEND=RHI_BACKEND_VULKAN
            ${SLANG_DEFINE_ARGS}
            -I "${SHADER_DIR}"
            -O${SPIRV_OPT_LEVEL}
            -matrix-layout-row-major
            -warnings-as-errors 31000
            ${DEBUG_INFO_FLAGS}
            -source-embed-style u32
            -source-embed-name "k${NAME}_${LABEL}_Spirv"
            -depfile "${SPIRV_DEPFILE}"
            -reflection-json "${STAGE_REFLECTION_JSON}"
            -o "${SPIRV_HEADER}"
        RESULT_VARIABLE SPIRV_RESULT
        ERROR_VARIABLE SPIRV_ERROR
    )
    if(NOT SPIRV_RESULT EQUAL 0)
        message(FATAL_ERROR "slangc failed compiling ${INPUT} (${STAGE}, spirv): ${SPIRV_ERROR}")
    endif()
    # ---- reflection: 从这个 stage 的反射 JSON 里挑出 set 2(pass 级 bind group)的资源绑定,
    # 和跨 stage 的累积表合并。覆盖 constantBuffer、2D/Cube 纹理(含深度纹理)、samplerState、
    # structuredBuffer(只读/RW)这几类, 其余资源种类暂时跳过不登记, 遇到时再补。
    file(READ "${STAGE_REFLECTION_JSON}" REFLECT_JSON)
    file(REMOVE "${STAGE_REFLECTION_JSON}")

    string(JSON PARAM_COUNT ERROR_VARIABLE PARAM_COUNT_ERR LENGTH "${REFLECT_JSON}" parameters)
    if(NOT PARAM_COUNT_ERR AND PARAM_COUNT GREATER 0)
        math(EXPR PARAM_LAST "${PARAM_COUNT} - 1")
        foreach(PIDX RANGE 0 ${PARAM_LAST})
            # push constant([[vk::push_constant]])在反射 JSON 里也出现在 parameters 数组里,
            # binding.kind 是 "pushConstantBuffer" 而不是 "descriptorTableSlot", 且和 set 0 的
            # 真实资源一样不带 "space" 字段——必须先按 kind 排除掉, 否则会被下面"没有 space
            # 字段就当 set 0"的逻辑误当成一个 set 0 的 BindGroupLayout 绑定收进去。
            string(JSON P_BINDING_KIND GET "${REFLECT_JSON}" parameters ${PIDX} binding kind)
            if(NOT P_BINDING_KIND STREQUAL "descriptorTableSlot")
                continue()
            endif()

            # slangc 的反射 JSON 只在 space 非 0 时才输出 "space" 字段(0 是省略掉的默认值)——
            # 所以 SPACE_ERR(取不到 space 字段)实际就代表 space 0, 不能当成"取不到就跳过"处理,
            # 否则纯工具 shader(bindings 全在 set 0)会被反射逻辑整个忽略掉。
            string(JSON P_SPACE ERROR_VARIABLE SPACE_ERR GET "${REFLECT_JSON}" parameters ${PIDX} binding space)
            if(SPACE_ERR)
                set(P_TARGET_PREFIX "S0_")
            elseif(P_SPACE EQUAL 2)
                set(P_TARGET_PREFIX "")
            else()
                continue()
            endif()

            string(JSON P_NAME GET "${REFLECT_JSON}" parameters ${PIDX} name)
            string(JSON P_INDEX GET "${REFLECT_JSON}" parameters ${PIDX} binding index)
            string(JSON P_TYPE_KIND GET "${REFLECT_JSON}" parameters ${PIDX} type kind)

            set(P_SUPPORTED TRUE)
            set(P_SAMPLER_TYPE "Filtering")
            set(P_STORAGE_FORMAT "Undefined")
            set(P_MULTISAMPLE "false")

            set(P_HAS_UNFILTERABLE_FLOAT FALSE)
            set(P_UNFILTERABLE_FLOAT_APPLIED FALSE)
            string(JSON P_ATTR_COUNT ERROR_VARIABLE ATTR_COUNT_ERR LENGTH "${REFLECT_JSON}" parameters ${PIDX} userAttribs)
            if(NOT ATTR_COUNT_ERR AND P_ATTR_COUNT GREATER 0)
                math(EXPR P_ATTR_LAST "${P_ATTR_COUNT} - 1")
                foreach(AIDX RANGE 0 ${P_ATTR_LAST})
                    string(JSON P_ATTR_NAME GET "${REFLECT_JSON}" parameters ${PIDX} userAttribs ${AIDX} name)
                    if(P_ATTR_NAME STREQUAL "UnfilterableFloat")
                        set(P_HAS_UNFILTERABLE_FLOAT TRUE)
                    endif()
                endforeach()
            endif()
            if(P_TYPE_KIND STREQUAL "constantBuffer")
                set(P_BINDING_TYPE "Uniform_Buffer")
                # ConstantBuffer<T> 在 slang/HLSL 里天生只读, 语法上没有任何写入方式——手写
                # BindGroupLayoutEntry 的地方(GlobalBindings.cpp/MaterialProxy.cpp 等)全部
                # 用的 Read_Only, 这里理应保持一致
                set(P_ACCESS "Read_Only")
                set(P_VIEW_TYPE "Texture_View_2D")
                set(P_SAMPLE_TYPE "Float")
            elseif(P_TYPE_KIND STREQUAL "samplerState")
                set(P_BINDING_TYPE "Sampler")
                set(P_ACCESS "Read_Only")
                set(P_VIEW_TYPE "Texture_View_2D")
                set(P_SAMPLE_TYPE "Float")
                # 反射 JSON 分辨不出 SamplerState(filtering)和 SamplerComparisonState(comparison)——
                # 两者都只有 {"kind":"samplerState"}, 没有额外字段。engine 里目前没有用到比较采样器,
                # 真用到时需要在 C++ 侧手动 override, 不能指望反射猜出来。
                # WebGPU 要求配合深度纹理 Gather/Load 的 sampler 声明成 Non_Filtering; 引擎里
                # "Point" 命名的 sampler 都是 SamplerCache 里 mag/min/mipmap 全 Nearest 的那种, 按名字识别即可。
                string(FIND "${P_NAME}" "Point" P_NAME_HAS_POINT)
                if(NOT P_NAME_HAS_POINT EQUAL -1)
                    set(P_SAMPLER_TYPE "Non_Filtering")
                endif()
            elseif(P_TYPE_KIND STREQUAL "resource")
                string(JSON P_BASE_SHAPE ERROR_VARIABLE SHAPE_ERR GET "${REFLECT_JSON}" parameters ${PIDX} type baseShape)
                if(SHAPE_ERR)
                    set(P_SUPPORTED FALSE)
                elseif(P_BASE_SHAPE STREQUAL "texture2D" OR P_BASE_SHAPE STREQUAL "textureCube" OR P_BASE_SHAPE STREQUAL "texture3D")
                    # 只读纹理(Texture2D/TextureCube/DepthTexture2D)完全不带 type.access 字段;
                    # 可写的存储纹理(WTexture2DArray 等, 引擎里用于 compute shader UAV 输出)带
                    # type.access == "write"(纯写)或 "readWrite", 且顶层多一个 format 字段
                    # (对应 [format("...")] 属性, 比如 "rgba16f")——用这两个信号区分 Sampled_Texture
                    # 和 Storage_Texture, 不能只看 baseShape(两者的 baseShape 是一样的)。
                    string(JSON P_TEX_ACCESS ERROR_VARIABLE TEX_ACCESS_ERR GET "${REFLECT_JSON}" parameters ${PIDX} type access)
                    string(JSON P_ARRAY ERROR_VARIABLE ARRAY_ERR GET "${REFLECT_JSON}" parameters ${PIDX} type array)
                    # Texture2DMS<T> 反射出来的 baseShape 仍然是 "texture2D", 只是多一个
                    # "multisample": true 字段(见 slangc -reflection-json 的实际输出), 不是
                    # 单独的 baseShape 值。
                    string(JSON P_IS_MULTISAMPLE ERROR_VARIABLE MULTISAMPLE_ERR GET "${REFLECT_JSON}" parameters ${PIDX} type multisample)
                    if(NOT MULTISAMPLE_ERR AND P_IS_MULTISAMPLE)
                        set(P_MULTISAMPLE "true")
                    endif()

                    if(P_BASE_SHAPE STREQUAL "textureCube")
                        set(P_VIEW_TYPE "Texture_View_Cube")
                    elseif(P_BASE_SHAPE STREQUAL "texture3D")
                        set(P_VIEW_TYPE "Texture_View_3D")
                    elseif(NOT ARRAY_ERR AND P_ARRAY)
                        set(P_VIEW_TYPE "Texture_View_2D_Array")
                    else()
                        set(P_VIEW_TYPE "Texture_View_2D")
                    endif()

                    if(NOT TEX_ACCESS_ERR AND (P_TEX_ACCESS STREQUAL "write" OR P_TEX_ACCESS STREQUAL "readWrite"))
                        set(P_BINDING_TYPE "Storage_Texture")
                        if(P_TEX_ACCESS STREQUAL "write")
                            set(P_ACCESS "Write_Only")
                        else()
                            set(P_ACCESS "Read_Write")
                        endif()
                        set(P_SAMPLE_TYPE "Float")
                        string(JSON P_FORMAT_STR ERROR_VARIABLE FORMAT_ERR GET "${REFLECT_JSON}" parameters ${PIDX} format)
                        if(FORMAT_ERR)
                            message(FATAL_ERROR "${INPUT}: storage texture '${P_NAME}' has no [format(...)] reflection - can't derive rhi::TextureFormat")
                        elseif(P_FORMAT_STR STREQUAL "rgba16f")
                            set(P_STORAGE_FORMAT "RGBA16_SFLOAT")
                        elseif(P_FORMAT_STR STREQUAL "r11f_g11f_b10f")
                            set(P_STORAGE_FORMAT "B10G11R11_UFLOAT_PACK32")
                        elseif(P_FORMAT_STR STREQUAL "rgba8")
                            set(P_STORAGE_FORMAT "RGBA8_UNORM")
                        elseif(P_FORMAT_STR STREQUAL "rgb10_a2")
                            set(P_STORAGE_FORMAT "A2BGR10_UNORM_PACK32")
                        elseif(P_FORMAT_STR STREQUAL "r32f")
                            set(P_STORAGE_FORMAT "R32_SFLOAT")
                        elseif(P_FORMAT_STR STREQUAL "rg32f")
                            set(P_STORAGE_FORMAT "RG32_SFLOAT")
                        elseif(P_FORMAT_STR STREQUAL "r32ui")
                            set(P_STORAGE_FORMAT "R32_UINT")
                        elseif(P_FORMAT_STR STREQUAL "r8ui")
                            set(P_STORAGE_FORMAT "R8_UINT")
                        elseif(P_FORMAT_STR STREQUAL "r16")
                            set(P_STORAGE_FORMAT "R16_UNORM")
                        elseif(P_FORMAT_STR STREQUAL "r16f")
                            set(P_STORAGE_FORMAT "R16_SFLOAT")
                        elseif(P_FORMAT_STR STREQUAL "r8")
                            set(P_STORAGE_FORMAT "R8_UNORM")
                        elseif(P_FORMAT_STR STREQUAL "rg8")
                            set(P_STORAGE_FORMAT "RG8_UNORM")
                        else()
                            message(FATAL_ERROR "${INPUT}: storage texture '${P_NAME}' has unmapped format '${P_FORMAT_STR}' - add it to CompilePassShader.cmake's format table")
                        endif()
                    else()
                        set(P_BINDING_TYPE "Sampled_Texture")
                        set(P_ACCESS "Read_Only")
                        # resultType 的标量类型信息(scalarType: "float32"/"uint32"/"int32" 等)在
                        # JSON 里的位置分两种形状: Texture2D<uint> 这种单通道纹理 resultType 本身
                        # 就是 {"kind":"scalar","scalarType":"uint32"}; Texture2D<float4> 这种
                        # 多通道纹理 resultType 是 {"kind":"vector","elementType":{"kind":"scalar",
                        # "scalarType":"float32"}}, 标量类型多包了一层 elementType。两种形状都要
                        # 处理, 不能只看 vector 这一种(单通道的 Texture2D<uint>/DepthTexture2D
                        # 都会落到 scalar 分支)。
                        string(JSON P_RESULT_KIND ERROR_VARIABLE RESULT_KIND_ERR GET "${REFLECT_JSON}" parameters ${PIDX} type resultType kind)
                        set(P_SCALAR_TYPE "")
                        if(NOT RESULT_KIND_ERR AND P_RESULT_KIND STREQUAL "scalar")
                            string(JSON P_SCALAR_TYPE ERROR_VARIABLE SCALAR_TYPE_ERR GET "${REFLECT_JSON}" parameters ${PIDX} type resultType scalarType)
                        elseif(NOT RESULT_KIND_ERR AND P_RESULT_KIND STREQUAL "vector")
                            string(JSON P_SCALAR_TYPE ERROR_VARIABLE SCALAR_TYPE_ERR GET "${REFLECT_JSON}" parameters ${PIDX} type resultType elementType scalarType)
                        endif()

                        if(P_SCALAR_TYPE MATCHES "^uint")
                            set(P_SAMPLE_TYPE "Uint")
                        elseif(P_SCALAR_TYPE MATCHES "^int")
                            set(P_SAMPLE_TYPE "Sint")
                        else()
                            # 浮点标量纹理反射拿不到 DepthTexture2D 这个专属类型——它和
                            # Texture2D<float>(单通道颜色纹理)反射出来的 JSON 完全一样(resultType
                            # 都是单个 float32 scalar), 没有任何字段能区分两者。只能退而求其次,
                            # 用资源名字里是否包含 "Depth" 作为约定俗成的信号, 而不是单靠 resultType
                            # 是不是 scalar 来判断(否则单通道颜色纹理会被误判成深度纹理)。
                            string(FIND "${P_NAME}" "Depth" P_NAME_HAS_DEPTH)
                            if(P_RESULT_KIND STREQUAL "scalar" AND NOT P_NAME_HAS_DEPTH EQUAL -1)
                                set(P_SAMPLE_TYPE "Depth")
                            elseif(P_HAS_UNFILTERABLE_FLOAT)
                                set(P_SAMPLE_TYPE "Unfilterable_Float")
                                set(P_UNFILTERABLE_FLOAT_APPLIED TRUE)
                            else()
                                set(P_SAMPLE_TYPE "Float")
                            endif()
                        endif()
                    endif()
                elseif(P_BASE_SHAPE STREQUAL "structuredBuffer")
                    set(P_BINDING_TYPE "Storage_Buffer")
                    set(P_VIEW_TYPE "Texture_View_2D")
                    set(P_SAMPLE_TYPE "Float")
                    # 只有 RWStructuredBuffer 才带 type.access == "readWrite" 这个字段,
                    # 普通只读 StructuredBuffer 完全不带 access 字段(不是带了个别的值)。
                    string(JSON P_RW_ACCESS ERROR_VARIABLE RW_ACCESS_ERR GET "${REFLECT_JSON}" parameters ${PIDX} type access)
                    if(NOT RW_ACCESS_ERR AND P_RW_ACCESS STREQUAL "readWrite")
                        set(P_ACCESS "Read_Write")
                    else()
                        set(P_ACCESS "Read_Only")
                    endif()
                else()
                    set(P_SUPPORTED FALSE)
                endif()
            else()
                set(P_SUPPORTED FALSE)
            endif()

            if(P_HAS_UNFILTERABLE_FLOAT AND NOT P_UNFILTERABLE_FLOAT_APPLIED)
                message(FATAL_ERROR "${INPUT}: [UnfilterableFloat] on '${P_NAME}' - only valid on a sampled float color texture")
            endif()

            if(NOT P_SUPPORTED)
                continue()
            endif()

            # 注意: 不能用 list(FIND ... EXISTING_POS) 之后靠 if(NOT EXISTING_POS EQUAL -1) 以外的
            # 布尔判断简化——更要注意的是下面 ALL_BINDING_INDICES 本身不能用 if(ALL_BINDING_INDICES)
            # 判断"是否非空"(CMake 陷阱: 当列表只有一个元素且该元素的字符串是 "0" 时,
            # if(<list>) 会把整个列表的字符串值当成布尔值 "0" 处理, 判定为假, 即使列表其实
            # 有一个元素——binding index 0 是极常见的情况(第一个资源多半分到 0), 必须用
            # list(LENGTH ...) 代替, 后面统计 ALL_BINDING_INDICES 是否为空同理。
            if(P_TARGET_PREFIX STREQUAL "S0_")
                list(FIND ALL_BINDING_INDICES_S0 "${P_INDEX}" EXISTING_POS_S0)
                if(EXISTING_POS_S0 EQUAL -1)
                    list(APPEND ALL_BINDING_INDICES_S0 "${P_INDEX}")
                    set(BINDING_S0_${P_INDEX}_NAME "${P_NAME}")
                    set(BINDING_S0_${P_INDEX}_TYPE "${P_BINDING_TYPE}")
                    set(BINDING_S0_${P_INDEX}_ACCESS "${P_ACCESS}")
                    set(BINDING_S0_${P_INDEX}_VIEWTYPE "${P_VIEW_TYPE}")
                    set(BINDING_S0_${P_INDEX}_SAMPLETYPE "${P_SAMPLE_TYPE}")
                    set(BINDING_S0_${P_INDEX}_SAMPLERTYPE "${P_SAMPLER_TYPE}")
                    set(BINDING_S0_${P_INDEX}_STORAGEFORMAT "${P_STORAGE_FORMAT}")
                    set(BINDING_S0_${P_INDEX}_MULTISAMPLE "${P_MULTISAMPLE}")
                    set(BINDING_S0_${P_INDEX}_STAGES "${LABEL}")
                else()
                    list(APPEND BINDING_S0_${P_INDEX}_STAGES "${LABEL}")
                endif()
            else()
                list(FIND ALL_BINDING_INDICES "${P_INDEX}" EXISTING_POS)
                if(EXISTING_POS EQUAL -1)
                    list(APPEND ALL_BINDING_INDICES "${P_INDEX}")
                    set(BINDING_${P_INDEX}_NAME "${P_NAME}")
                    set(BINDING_${P_INDEX}_TYPE "${P_BINDING_TYPE}")
                    set(BINDING_${P_INDEX}_ACCESS "${P_ACCESS}")
                    set(BINDING_${P_INDEX}_VIEWTYPE "${P_VIEW_TYPE}")
                    set(BINDING_${P_INDEX}_SAMPLETYPE "${P_SAMPLE_TYPE}")
                    set(BINDING_${P_INDEX}_SAMPLERTYPE "${P_SAMPLER_TYPE}")
                    set(BINDING_${P_INDEX}_STORAGEFORMAT "${P_STORAGE_FORMAT}")
                    set(BINDING_${P_INDEX}_MULTISAMPLE "${P_MULTISAMPLE}")
                    set(BINDING_${P_INDEX}_STAGES "${LABEL}")
                else()
                    list(APPEND BINDING_${P_INDEX}_STAGES "${LABEL}")
                endif()
            endif()
        endforeach()
    endif()

    file(READ "${SPIRV_HEADER}" SPIRV_CONTENT)
    file(REMOVE "${SPIRV_HEADER}")
    string(APPEND GENERATED_CONTENT "    inline ${SPIRV_CONTENT}\n")

    file(READ "${SPIRV_DEPFILE}" SPIRV_DEP_CONTENT)
    file(REMOVE "${SPIRV_DEPFILE}")
    # Windows 路径的盘符冒号(C\:)在 Make 语法里也带转义,不能拿"第一个冒号"当规则分隔符——
    # 真正的分隔符是 ": "(冒号+空格),盘符冒号后面跟的是 / 或 \,不会有空格
    string(FIND "${SPIRV_DEP_CONTENT}" ": " SPIRV_DEP_COLON)
    math(EXPR SPIRV_DEP_START "${SPIRV_DEP_COLON} + 2")
    string(SUBSTRING "${SPIRV_DEP_CONTENT}" ${SPIRV_DEP_START} -1 SPIRV_DEP_LIST)
    string(APPEND ALL_DEPS " ${SPIRV_DEP_LIST}")

    # ---- WGSL: slangc keeps the source entry name, engine convention requires "main" ----
    set(WGSL_RAW "${TMP_DIR}/${NAME}_${LABEL}.wgsl.tmp")
    set(WGSL_DEPFILE "${TMP_DIR}/${NAME}_${LABEL}.wgsl.d")
    # -O0: 和迁移前运行时编译(ShaderCompiler::compileToWgsls, optimize=false -> SLANG_OPTIMIZATION_LEVEL_NONE)保持一致
    # -matrix-layout-row-major: 原因同上(SPIRV 那部分注释)
    execute_process(
        COMMAND "${SLANGC}" "${INPUT}"
            -entry "${ENTRY}" -stage "${STAGE}"
            -target wgsl
            -D RHI_BACKEND=RHI_BACKEND_WEBGPU
            ${SLANG_DEFINE_ARGS}
            -I "${SHADER_DIR}"
            -O0
            -matrix-layout-row-major
            -warnings-as-errors 31000
            ${DEBUG_INFO_FLAGS}
            -depfile "${WGSL_DEPFILE}"
            -o "${WGSL_RAW}"
        RESULT_VARIABLE WGSL_RESULT
        ERROR_VARIABLE WGSL_ERROR
    )
    if(NOT WGSL_RESULT EQUAL 0)
        message(FATAL_ERROR "slangc failed compiling ${INPUT} (${STAGE}, wgsl): ${WGSL_ERROR}")
    endif()
    file(READ "${WGSL_RAW}" WGSL_CONTENT)
    file(REMOVE "${WGSL_RAW}")

    file(READ "${WGSL_DEPFILE}" WGSL_DEP_CONTENT)
    file(REMOVE "${WGSL_DEPFILE}")
    # 同上:真正的规则分隔符是 ": "(冒号+空格),不能拿"第一个冒号"(可能是盘符转义 C\:)当分隔符
    string(FIND "${WGSL_DEP_CONTENT}" ": " WGSL_DEP_COLON)
    math(EXPR WGSL_DEP_START "${WGSL_DEP_COLON} + 2")
    string(SUBSTRING "${WGSL_DEP_CONTENT}" ${WGSL_DEP_START} -1 WGSL_DEP_LIST)
    string(APPEND ALL_DEPS " ${WGSL_DEP_LIST}")

    # rename the entry function to "main" (WGSL has no CLI equivalent of renameEntryPoint)
    string(REPLACE "${ENTRY}" "main" WGSL_CONTENT "${WGSL_CONTENT}")

    # patch the push-constant global (see comment near PUSH_CONSTANT_NAME above) to use
    # WGSL's real immediate address space instead of Slang's plain-uniform fallback
    if(PUSH_CONSTANT_NAME)
        string(REGEX REPLACE "var<uniform> (${PUSH_CONSTANT_NAME}_[0-9]+) :" "var<immediate> \\1 :" WGSL_CONTENT "${WGSL_CONTENT}")
        string(PREPEND WGSL_CONTENT "requires immediate_address_space;\n")
    endif()

    hyp_patch_wgsl_storage_formats(WGSL_CONTENT 2 "BINDING_" "${ALL_BINDING_INDICES}" "${INPUT}" "${STAGE}")
    hyp_patch_wgsl_storage_formats(WGSL_CONTENT 0 "BINDING_S0_" "${ALL_BINDING_INDICES_S0}" "${INPUT}" "${STAGE}")

    # escape into a C string literal: backslashes, then quotes, then newlines (order matters)
    string(REPLACE "\\" "\\\\" WGSL_CONTENT "${WGSL_CONTENT}")
    string(REPLACE "\"" "\\\"" WGSL_CONTENT "${WGSL_CONTENT}")
    string(REPLACE "\n" "\\n" WGSL_CONTENT "${WGSL_CONTENT}")

    string(APPEND GENERATED_CONTENT "    inline constexpr char k${NAME}_${LABEL}_Wgsl[] = \"${WGSL_CONTENT}\";\n\n")
endforeach()

# 所有 stage 都处理完之后, 按 binding index 升序把累积表落成一份 constexpr 数组——排序只是为了
# 让生成结果稳定、可读, 和实际渲染行为无关(BindGroupLayout 不要求 entries 按 binding 排序)。
# 用 list(LENGTH) 判断是否非空, 不用 if(ALL_BINDING_INDICES)(原因见上面 EXISTING_POS 旁的注释)。
#
# set 2 完全没有任何绑定时(纯工具 shader, 不 #include Binding.slang), 才退回去用 set 0 收集到的
# 结果——两者不会同时非空, 因为一个 shader 要么走标准的 [全局,scene/view,pass] 三段式 set 约定
# (只用 set 2), 要么完全不 include Binding.slang、自己的资源全在 set 0, 不存在两边都有东西
# 需要合并的情况。
list(LENGTH ALL_BINDING_INDICES BINDING_COUNT)
if(BINDING_COUNT GREATER 0)
    set(EMIT_INDICES ${ALL_BINDING_INDICES})
    set(EMIT_PREFIX "BINDING_")
else()
    list(LENGTH ALL_BINDING_INDICES_S0 BINDING_COUNT_S0)
    if(BINDING_COUNT_S0 GREATER 0)
        set(EMIT_INDICES ${ALL_BINDING_INDICES_S0})
        set(EMIT_PREFIX "BINDING_S0_")
    else()
        set(EMIT_INDICES "")
    endif()
endif()

list(LENGTH EMIT_INDICES EMIT_COUNT)
if(EMIT_COUNT GREATER 0)
    list(SORT EMIT_INDICES COMPARE NATURAL)
    string(APPEND GENERATED_CONTENT "    inline constexpr hyp::rhi::PassShaderBindingDesc k${NAME}_Bindings[] = {\n")
    foreach(IDX ${EMIT_INDICES})
        list(JOIN ${EMIT_PREFIX}${IDX}_STAGES " | hyp::rhi::ShaderStage::" STAGE_FLAGS_EXPR)
        string(PREPEND STAGE_FLAGS_EXPR "hyp::rhi::ShaderStage::")
        string(APPEND GENERATED_CONTENT
            "        { \"${${EMIT_PREFIX}${IDX}_NAME}\", ${IDX}, hyp::rhi::BindingType::${${EMIT_PREFIX}${IDX}_TYPE}, hyp::rhi::BindingAccess::${${EMIT_PREFIX}${IDX}_ACCESS}, 1, hyp::rhi::TextureViewType::${${EMIT_PREFIX}${IDX}_VIEWTYPE}, hyp::rhi::TextureFormat::${${EMIT_PREFIX}${IDX}_STORAGEFORMAT}, hyp::rhi::TextureSampleType::${${EMIT_PREFIX}${IDX}_SAMPLETYPE}, ${${EMIT_PREFIX}${IDX}_MULTISAMPLE}, hyp::rhi::SamplerBindingType::${${EMIT_PREFIX}${IDX}_SAMPLERTYPE}, ${STAGE_FLAGS_EXPR} },\n"
        )
    endforeach()
    string(APPEND GENERATED_CONTENT "    };\n\n")
endif()

string(APPEND GENERATED_CONTENT "}\n")

file(WRITE "${OUTPUT}" "${GENERATED_CONTENT}")

# Makefile 规则里冒号是分隔符,Windows 路径的盘符冒号要转义成 \: (和 slangc 自己吐出来的格式一致)
string(REPLACE ":" "\\:" OUTPUT_ESCAPED "${OUTPUT}")
file(WRITE "${OUTPUT}.d" "${OUTPUT_ESCAPED}:${ALL_DEPS}\n")
