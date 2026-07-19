include("${CMAKE_SOURCE_DIR}/CMake/PassShaderCodegen.cmake")

# 只负责把 Shader/*.slang 预编译成 GeneratedShaders/$<CONFIG>/<Name>.generated.h,不编译/链接任何
# C++,方便在 VS solution explorer 里单独对着这个工程生成、快速迭代 shader,不用管其它库的 C++
# 编译/链接。Renderer(生成头文件的唯一使用方)通过 add_dependencies + target_include_directories
# 依赖这个工程——这样每份 shader 只在这一个工程里注册一次 add_custom_command
add_custom_target(PassShaders)

add_pass_shader(PassShaders Present "${CMAKE_SOURCE_DIR}/Shader/Common/Present.slang")
add_pass_shader(PassShaders Culling "${CMAKE_SOURCE_DIR}/Shader/GSplat/Culling.slang")
add_pass_shader(PassShaders GSplat "${CMAKE_SOURCE_DIR}/Shader/GSplat/GSplat.slang")
add_pass_shader(PassShaders GSplatDepth "${CMAKE_SOURCE_DIR}/Shader/GSplat/GSplatDepth.slang")
add_pass_shader(PassShaders SkyLighting "${CMAKE_SOURCE_DIR}/Shader/Material/SkyLighting.slang")
add_pass_shader(PassShaders SkyCubemap "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/SkyCubemap.slang")
add_pass_shader(PassShaders CubemapMip "${CMAKE_SOURCE_DIR}/Shader/Common/CubemapMip.slang")
add_pass_shader(PassShaders SHCoeffGather "${CMAKE_SOURCE_DIR}/Shader/Common/SHCoeffGather.slang")
add_pass_shader(PassShaders SHCoeffReduce "${CMAKE_SOURCE_DIR}/Shader/Common/SHCoeffReduce.slang")
add_pass_shader(PassShaders CubemapPrefilter "${CMAKE_SOURCE_DIR}/Shader/Common/CubemapPrefilter.slang")
add_pass_shader(PassShaders LightVolumeInject "${CMAKE_SOURCE_DIR}/Shader/Common/LightVolumeInject.slang")
add_pass_shader(PassShaders DirectLighting "${CMAKE_SOURCE_DIR}/Shader/Material/DirectLighting.slang")
add_pass_shader(PassShaders TransmittanceLut "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/TransmittanceLut.slang")
add_pass_shader(PassShaders MultiScatteringLut "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/MultiScatteringLut.slang")
add_pass_shader(PassShaders SkyViewLut "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/SkyViewLut.slang")
add_pass_shader(PassShaders AerialPerspectiveLut "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/AerialPerspectiveLut.slang")
add_pass_shader(PassShaders Atmosphere "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/Atmosphere.slang")
add_pass_shader(PassShaders RadixSortUpsweep "${CMAKE_SOURCE_DIR}/Shader/Common/RadixSort/Upsweep.slang")
add_pass_shader(PassShaders RadixSortSpine "${CMAKE_SOURCE_DIR}/Shader/Common/RadixSort/Spine.slang")
add_pass_shader(PassShaders RadixSortDownsweep "${CMAKE_SOURCE_DIR}/Shader/Common/RadixSort/Downsweep.slang")

# GSplat2(Source/Test/GSplat2, ChunkAllocator 驱动的稀疏 LOD 流式加载原型): 独立于生产版本的
# Shader/GSplat/* —— 剔除 shader 需要感知 chunk 布局, 渲染 shader 没有 instance 变换,
# 因此单独放在 Shader/GSplat2/ 目录, 用不同的 NAME 避免和上面的 Culling/GSplat 撞掉
add_pass_shader(PassShaders GSplat2Culling "${CMAKE_SOURCE_DIR}/Shader/GSplat2/Culling.slang")
add_pass_shader(PassShaders GSplat2Render "${CMAKE_SOURCE_DIR}/Shader/GSplat2/GSplat.slang")
