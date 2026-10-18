include("${CMAKE_SOURCE_DIR}/CMake/PassShaderCodegen.cmake")

# 只负责把 Shader/*.slang 预编译成 GeneratedShaders/$<CONFIG>/<Name>.generated.h,不编译/链接任何
# C++,方便在 VS solution explorer 里单独对着这个工程生成、快速迭代 shader,不用管其它库的 C++
# 编译/链接。RenderBase 和 Renderer(生成头文件的使用方)通过 add_dependencies +
# target_include_directories 依赖这个工程——这样每份 shader 只在这一个工程里注册一次 add_custom_command
add_custom_target(PassShaders)

add_pass_shader(PassShaders CopyTexture "${CMAKE_SOURCE_DIR}/Shader/Common/CopyTexture.slang")
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
add_pass_shader(PassShaders LightVolume "${CMAKE_SOURCE_DIR}/Shader/Material/LightVolume.slang")
add_pass_shader(PassShaders VisualizationIcon "${CMAKE_SOURCE_DIR}/Shader/Visualization/Icon.slang")
add_pass_shader(PassShaders VisualizationPoint "${CMAKE_SOURCE_DIR}/Shader/Visualization/Point.slang")
add_pass_shader(PassShaders VisualizationLine "${CMAKE_SOURCE_DIR}/Shader/Visualization/Line.slang")
add_pass_shader(PassShaders VisualizationWireframeBox "${CMAKE_SOURCE_DIR}/Shader/Visualization/WireframeBox.slang")
add_pass_shader(PassShaders VisualizationWireframeSphere "${CMAKE_SOURCE_DIR}/Shader/Visualization/WireframeSphere.slang")
add_pass_shader(PassShaders VisualizationWireframeCapsule "${CMAKE_SOURCE_DIR}/Shader/Visualization/WireframeCapsule.slang")
add_pass_shader(PassShaders VisualizationWireframeCylinder "${CMAKE_SOURCE_DIR}/Shader/Visualization/WireframeCylinder.slang")
add_pass_shader(PassShaders VisualizationWireframeLightCone "${CMAKE_SOURCE_DIR}/Shader/Visualization/WireframeLightCone.slang")
add_pass_shader(PassShaders VisualizationWireframeGeometry "${CMAKE_SOURCE_DIR}/Shader/Visualization/WireframeGeometry.slang")
add_pass_shader(PassShaders VisualizationComposite "${CMAKE_SOURCE_DIR}/Shader/Visualization/Composite.slang")
add_pass_shader(PassShaders CopyDepth "${CMAKE_SOURCE_DIR}/Shader/Common/CopyDepth.slang")
add_pass_shader(PassShaders ShadowMask "${CMAKE_SOURCE_DIR}/Shader/Material/ShadowMask.slang")
add_pass_shader(PassShaders TransmittanceLut "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/TransmittanceLut.slang")
add_pass_shader(PassShaders MultiScatteringLut "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/MultiScatteringLut.slang")
add_pass_shader(PassShaders SkyViewLut "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/SkyViewLut.slang")
add_pass_shader(PassShaders AerialPerspectiveLut "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/AerialPerspectiveLut.slang")
add_pass_shader(PassShaders Atmosphere "${CMAKE_SOURCE_DIR}/Shader/Atmosphere/Atmosphere.slang")
add_pass_shader(PassShaders ColorGradingLut "${CMAKE_SOURCE_DIR}/Shader/PostProcess/ColorGradingLut.slang")
add_pass_shader(PassShaders ColorGrading "${CMAKE_SOURCE_DIR}/Shader/PostProcess/ColorGrading.slang")
add_pass_shader(PassShaders TAA "${CMAKE_SOURCE_DIR}/Shader/PostProcess/TAA.slang")
add_pass_shader(PassShaders Downsample "${CMAKE_SOURCE_DIR}/Shader/PostProcess/Downsample.slang")
add_pass_shader(PassShaders BloomDownsample "${CMAKE_SOURCE_DIR}/Shader/PostProcess/BloomDownsample.slang")
add_pass_shader(PassShaders BloomUpsample "${CMAKE_SOURCE_DIR}/Shader/PostProcess/BloomUpsample.slang")
add_pass_shader(PassShaders HistogramAtomic "${CMAKE_SOURCE_DIR}/Shader/PostProcess/HistogramAtomic.slang")
add_pass_shader(PassShaders HistogramConvert "${CMAKE_SOURCE_DIR}/Shader/PostProcess/HistogramConvert.slang")
add_pass_shader(PassShaders AutoExposure "${CMAKE_SOURCE_DIR}/Shader/PostProcess/AutoExposure.slang")
add_pass_shader(PassShaders SSAOSetup "${CMAKE_SOURCE_DIR}/Shader/PostProcess/SSAOSetup.slang")
add_pass_shader(PassShaders SSAOHalfRes "${CMAKE_SOURCE_DIR}/Shader/PostProcess/SSAOHalfRes.slang")
add_pass_shader(PassShaders SSAOFullRes "${CMAKE_SOURCE_DIR}/Shader/PostProcess/SSAOFullRes.slang")
add_pass_shader(PassShaders AOGenerateGTAO "${CMAKE_SOURCE_DIR}/Shader/PostProcess/AOGenerateGTAO.slang")
add_pass_shader(PassShaders AOSpatialFilterGTAO "${CMAKE_SOURCE_DIR}/Shader/PostProcess/AOSpatialFilterGTAO.slang")
add_pass_shader(PassShaders AOTemporalFilterGTAO "${CMAKE_SOURCE_DIR}/Shader/PostProcess/AOTemporalFilterGTAO.slang")
add_pass_shader(PassShaders HZBSeed "${CMAKE_SOURCE_DIR}/Shader/Common/HZBSeed.slang")
add_pass_shader(PassShaders HZBReduce "${CMAKE_SOURCE_DIR}/Shader/Common/HZBReduce.slang")
add_pass_shader(PassShaders RadixSortUpsweep "${CMAKE_SOURCE_DIR}/Shader/Common/RadixSort/Upsweep.slang")
add_pass_shader(PassShaders RadixSortSpine "${CMAKE_SOURCE_DIR}/Shader/Common/RadixSort/Spine.slang")
add_pass_shader(PassShaders RadixSortDownsweep "${CMAKE_SOURCE_DIR}/Shader/Common/RadixSort/Downsweep.slang")

# MeshSDF(Source/Test/MeshSDF): 接入 Scene/MeshRenderProxy 体系、从 DistanceFieldBrickAtlas 共享
# brick atlas 采样的测试 Pass, 见 Source/Runtime/Renderer/RenderPass/MeshSDFPass.cpp
add_pass_shader(PassShaders MeshSDFScene "${CMAKE_SOURCE_DIR}/Shader/MeshSDF/MeshSDFScene.slang")

# 把 CardCapturePass 画进临时捕获图集的一页拷进 SurfaceCacheAtlas 物理图集的几何层,
# 见 Source/Runtime/Renderer/RenderPass/CardCaptureCopyPass.cpp
add_pass_shader(PassShaders CardCaptureCopy "${CMAKE_SOURCE_DIR}/Shader/Material/CardCaptureCopy.slang")

# 新捕获页的光照状态重置, 见 Source/Runtime/Renderer/RenderPass/CardLightingResetPass.cpp
add_pass_shader(PassShaders CardLightingReset "${CMAKE_SOURCE_DIR}/Shader/Material/CardLightingReset.slang")

# Global Distance Field 合成: 把 hyp::GlobalDistanceField::update() 剔选出的候选物体列表
# 合成写进 GDF 自己的 brick atlas, 见 Source/Runtime/Renderer/GlobalDistanceFieldComposite.cpp
add_pass_shader(PassShaders GlobalDistanceFieldComposite "${CMAKE_SOURCE_DIR}/Shader/GlobalDistanceField/GlobalDistanceFieldComposite.slang")

# Global/Mesh Distance Field 可视化: 沿相机射线求交(GDF 粗测距或 MDF 对候选物体各自原始
# per-mesh SDF 精确 sphere tracing, 由 push constant 里的开关二选一)+ 命中反查 + surface
# cache 采色, 见 Source/Runtime/Renderer/RenderPass/GlobalDistanceFieldVisualizerPass.cpp
add_pass_shader(PassShaders GlobalDistanceFieldVisualizer "${CMAKE_SOURCE_DIR}/Shader/GlobalDistanceField/GlobalDistanceFieldVisualizer.slang")

# 屏幕空间 HZB 追踪的临时调试可视化, 验证 HZBTrace.slang 正确性, 见
# Source/Runtime/Renderer/RenderPass/ScreenTraceVisualizerPass.cpp
add_pass_shader(PassShaders ScreenTraceVisualizer "${CMAKE_SOURCE_DIR}/Shader/PostProcess/ScreenTraceVisualizer.slang")

# Screen Probe Gather 探针网格布点(统一网格降采样 + 自适应探针分配), 见
# Source/Runtime/Renderer/RenderPass/ScreenProbePlacementPass.cpp
add_pass_shader(PassShaders ScreenProbeDownsampleUniform "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeDownsampleUniform.slang")
add_pass_shader(PassShaders ScreenProbeAdaptivePlacement "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeAdaptivePlacement.slang")
add_pass_shader(PassShaders ScreenProbeSetupIndirectArgs "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeSetupIndirectArgs.slang")

# 屏幕探针追踪(M3a), 见 Source/Runtime/Renderer/RenderPass/ScreenProbeTracePass.cpp
add_pass_shader(PassShaders ScreenProbeTrace "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeTrace.slang")
add_pass_shader(PassShaders ScreenProbeSceneDepthHistoryCopy "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeSceneDepthHistoryCopy.slang")

# 屏幕探针追踪结果合成(M5.1), 见 Source/Runtime/Renderer/RenderPass/ScreenProbeFilteringPass.cpp
add_pass_shader(PassShaders ScreenProbeCompositeTraces "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeCompositeTraces.slang")
# 屏幕探针空间滤波(M4)+ 辐射度转辐照度图集(M5.2), 同样在 ScreenProbeFilteringPass.cpp
add_pass_shader(PassShaders ScreenProbeCalculateMoving "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeCalculateMoving.slang")
add_pass_shader(PassShaders ScreenProbeFilterGatherTraces "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeFilterGatherTraces.slang")
add_pass_shader(PassShaders ScreenProbeConvertToIrradiance "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeConvertToIrradiance.slang")

# 屏幕探针积分(M5.3)和合成进 SceneColor(M5.4), 见 Source/Runtime/Renderer/RenderPass/ScreenProbeIntegratePass.cpp、
# ScreenProbeDiffuseIndirectCompositePass.cpp
add_pass_shader(PassShaders ScreenProbeIntegrate "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeIntegrate.slang")
add_pass_shader(PassShaders ScreenProbeDiffuseIndirectComposite "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeDiffuseIndirectComposite.slang")
# 屏幕探针漫反射间接光时域重投影(M6), 见 Source/Runtime/Renderer/RenderPass/ScreenProbeTemporalFilterPass.cpp
add_pass_shader(PassShaders ScreenProbeTemporalFilter "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeTemporalFilter.slang")

# Short Range AO(M9), 见 Source/Runtime/Renderer/RenderPass/ScreenProbeShortRangeAOPass.cpp
add_pass_shader(PassShaders ScreenProbeShortRangeAODownsample "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeShortRangeAODownsample.slang")
add_pass_shader(PassShaders ScreenProbeShortRangeAOUpsampleWeights "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeShortRangeAOUpsampleWeights.slang")
add_pass_shader(PassShaders ScreenProbeShortRangeAO "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeShortRangeAO.slang")

# 结构化重要性采样(M7.1: BRDF PDF, M7.2: Lighting PDF, M7.3: GenerateRays), 见
# Source/Runtime/Renderer/RenderPass/ScreenProbeImportanceSamplingPass.cpp
add_pass_shader(PassShaders ScreenProbeBRDFPDF "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeBRDFPDF.slang")
add_pass_shader(PassShaders ScreenProbeLightingPDF "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeLightingPDF.slang")
add_pass_shader(PassShaders ScreenProbeLightingHistoryCopy "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeLightingHistoryCopy.slang")
add_pass_shader(PassShaders ScreenProbeGenerateRays "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeGenerateRays.slang")

# 屏幕探针布点的临时调试可视化(统一/自适应探针各画一个彩色十字标记), 见
# Source/Runtime/Renderer/RenderPass/ScreenProbeVisualizerPass.cpp
add_pass_shader(PassShaders ScreenProbeBuildDrawIndirect "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeBuildDrawIndirect.slang")
add_pass_shader(PassShaders ScreenProbeVisualizer "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeVisualizer.slang")

# Card Surface Cache 直接光照: 方向光阴影追 per-mesh MDF, 点/聚光阴影追 GDF, 结果写进
# SurfaceCacheAtlas::DirectLightingAtlas, 见 Source/Runtime/Renderer/RenderPass/CardDirectLightingPass.cpp
add_pass_shader(PassShaders CardDirectLighting "${CMAKE_SOURCE_DIR}/Shader/Material/CardDirectLighting.slang")

# Surface Cache 最终光照合成: FinalLighting = (Direct+Indirect)·Albedo/π + Emissive, 结果写进
# SurfaceCacheAtlas::FinalLightingAtlas, 见 Source/Runtime/Renderer/RenderPass/CombineFinalLightingPass.cpp
add_pass_shader(PassShaders CombineFinalLighting "${CMAKE_SOURCE_DIR}/Shader/Material/CombineFinalLighting.slang")

# 存储纹理清零, 见 Source/Runtime/RenderBase/TextureClearer.cpp。存储纹理的格式写死在 shader 的
# [format(...)] 里, 所以每种格式各编译一份; 覆盖 CompilePassShader.cmake 反射表里的全部存储格式
set(CLEAR_STORAGE_TEXTURE_SHADER "${CMAKE_SOURCE_DIR}/Shader/Common/ClearStorageTexture.slang")
add_pass_shader(PassShaders ClearStorageTexture_RGBA16F "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=rgba16f STORAGE_TEXEL=float4)
add_pass_shader(PassShaders ClearStorageTexture_R11G11B10F "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=r11f_g11f_b10f STORAGE_TEXEL=float4)
add_pass_shader(PassShaders ClearStorageTexture_RGBA8 "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=rgba8 STORAGE_TEXEL=float4)
add_pass_shader(PassShaders ClearStorageTexture_RGB10A2 "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=rgb10_a2 STORAGE_TEXEL=float4)
add_pass_shader(PassShaders ClearStorageTexture_R32F "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=r32f STORAGE_TEXEL=float)
add_pass_shader(PassShaders ClearStorageTexture_RG32F "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=rg32f STORAGE_TEXEL=float2)
add_pass_shader(PassShaders ClearStorageTexture_R32UI "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=r32ui STORAGE_TEXEL=uint)
add_pass_shader(PassShaders ClearStorageTexture_R8UI "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=r8ui STORAGE_TEXEL=uint)
add_pass_shader(PassShaders ClearStorageTexture_R16 "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=r16 STORAGE_TEXEL=float)
add_pass_shader(PassShaders ClearStorageTexture_R16F "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=r16f STORAGE_TEXEL=float)
add_pass_shader(PassShaders ClearStorageTexture_R8 "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=r8 STORAGE_TEXEL=float)
add_pass_shader(PassShaders ClearStorageTexture_RG8 "${CLEAR_STORAGE_TEXTURE_SHADER}" DEFINES STORAGE_FORMAT=rg8 STORAGE_TEXEL=float2)

# Radiosity(Surface Cache 间接光)[A]/[B]/[C]/[D] 四个 pass, 见
# Source/Runtime/Renderer/RenderPass/RadiosityPass.cpp、Document/Lumen_Radiosity_Plan.md
add_pass_shader(PassShaders RadiosityTrace "${CMAKE_SOURCE_DIR}/Shader/Material/RadiosityTrace.slang")
add_pass_shader(PassShaders RadiosityFilter "${CMAKE_SOURCE_DIR}/Shader/Material/RadiosityFilter.slang")
add_pass_shader(PassShaders RadiosityConvertToSH "${CMAKE_SOURCE_DIR}/Shader/Material/RadiosityConvertToSH.slang")
add_pass_shader(PassShaders RadiosityIntegrate "${CMAKE_SOURCE_DIR}/Shader/Material/RadiosityIntegrate.slang")

# Lumen 镜面反射(R1: GenerateRays), 见 Source/Runtime/Renderer/RenderPass/ReflectionGenerateRaysPass.cpp、
# Document/Lumen_Reflections_Plan.md
add_pass_shader(PassShaders ReflectionGenerateRays "${CMAKE_SOURCE_DIR}/Shader/Reflection/ReflectionGenerateRays.slang")
# Lumen 镜面反射(R2: Trace), 见 Source/Runtime/Renderer/RenderPass/ReflectionTracePass.cpp
add_pass_shader(PassShaders ReflectionTrace "${CMAKE_SOURCE_DIR}/Shader/Reflection/ReflectionTrace.slang")
# Lumen 镜面反射(R3: Resolve), 见 Source/Runtime/Renderer/RenderPass/ReflectionResolvePass.cpp
add_pass_shader(PassShaders ReflectionResolve "${CMAKE_SOURCE_DIR}/Shader/Reflection/ReflectionResolve.slang")
# Lumen 镜面反射(R4: Temporal), 见 Source/Runtime/Renderer/RenderPass/ReflectionDenoiserTemporalPass.cpp
add_pass_shader(PassShaders ReflectionDenoiserTemporal "${CMAKE_SOURCE_DIR}/Shader/Reflection/ReflectionDenoiserTemporal.slang")
# Lumen 镜面反射(R6: 粗糙镜面读的带边框辐射度图集), 见 Source/Runtime/Renderer/RenderPass/ScreenProbeFilteringPass.cpp
add_pass_shader(PassShaders ScreenProbeFixupBorders "${CMAKE_SOURCE_DIR}/Shader/ScreenProbe/ScreenProbeFixupBorders.slang")
# Lumen 镜面反射(R5: Spatial), 见 Source/Runtime/Renderer/RenderPass/ReflectionDenoiserSpatialPass.cpp
add_pass_shader(PassShaders ReflectionDenoiserSpatial "${CMAKE_SOURCE_DIR}/Shader/Reflection/ReflectionDenoiserSpatial.slang")
