#pragma once
#include <cstdint>

namespace hyp
{
    // 跟 Shader/GlobalDistanceField/GlobalDistanceFieldVisualizer.slang 里的
    // kLumenVisualizeMode* 常量必须逐项一致。GDF/MDF 是跟 1-14 这些值正交的独立开关(见
    // SceneProxy::getUseMeshDistanceField()), 不是这个枚举的一个值——除了 Atlas 系列(直接
    // 显示图集本身, 不追踪, 这个开关对它们没有效果), 每一项命中着色都能在 GDF 粗测距/MDF
    // 精确求交之间切换。1-14 只在 scene.getLumenEnabled() 为真时才生效(见
    // RenderPipeline.cpp 的三段式分派), ScreenTrace* 两项则完全独立于这个开关, 在正常
    // 延迟管线里对屏幕已光栅化内容取值, 不属于 Lumen/Surface Cache 那一套——枚举本身已经
    // 不是"Lumen 专属"的调试视图集合了, 命名按这个来
    enum class SceneVisualizeMode : uint32_t
    {
        // 不替换管线, 走正常延迟渲染
        None = 0,
        GeometryNormal = 1,
        Albedo = 2,
        Normal = 3,
        Emissive = 4,
        CardWeights = 5,
        // 只是 DirectLightingAtlas 原始采样, 不乘 albedo——跟 FinalLighting 的区别见后者注释
        DirectLighting = 6,
        IndirectLighting = 7,
        // 命中着色返回的是 (Direct+Indirect)·Albedo/π + Emissive, 想单独看不乘 albedo 的
        // 纯 Direct/Indirect 项用上面两个模式
        FinalLighting = 8,
        AtlasAlbedo = 9,
        AtlasNormal = 10,
        AtlasDepth = 11,
        AtlasDirectLighting = 12,
        AtlasIndirectLighting = 13,
        AtlasFinalLighting = 14,
        // 屏幕空间 HZB 追踪的临时调试可视化(ScreenTraceVisualizerPass), 验证
        // HZBTrace.slang 的正确性, 不是 Screen Probe Gather 正式管线的一部分
        ScreenTraceHitState = 15,
        ScreenTraceRadiance = 16,
        // Screen Probe Gather 探针布点的调试可视化(ScreenProbeVisualizerPass): 统一探针画绿色
        // 十字、自适应探针画红色十字, 叠在正常光照结果上, 用来验证自适应探针是否贴着深度/法线
        // 突变边缘分布——跟 ScreenTrace* 一样独立于 getLumenEnabled(), 挂载方式也相同(等正常
        // 管线含 TAA/ColorGrading 跑完再叠加最后一笔), 见 RenderPipeline.cpp
        ScreenProbePlacement = 17,
        // 探针标记按这个探针 64 条追踪光线的平均辐射度染色(ScreenProbeVisualizerPass)
        ScreenProbeRadiance = 18,
        // 探针标记按辐照度图内部 texel 的平均值除以 π 染色, 应与 ScreenProbeRadiance 的颜色一致
        ScreenProbeIrradiance = 19,
        // 探针标记按 BRDF PDF(M7.1, ScreenProbeImportanceSamplingPass)在固定方向上的取值染色灰度
        ScreenProbeBRDFPDF = 20,
    };

    enum class ProjectionType
    {
        Perspective,
        Orthographic,
    };

    /*template <> Type* Reflection::createType<ProjectionType>()
    {
        auto e = new TEnumeration<ProjectionType>("ProjectionType");
        e->addValue("Perspective", ProjectionType::Perspective);
        e->addValue("Orthographic", ProjectionType::Orthographic);
        return e;
    }*/

    enum class ProjectionAxis
    {
        Horizontal,
        Vertical,
    };

    /*template <> Type* Reflection::createType<ProjectionAxis>()
    {
        auto e = new TEnumeration<ProjectionAxis>("ProjectionAxis");
        e->addValue("Horizontal", ProjectionAxis::Horizontal);
        e->addValue("Vertical", ProjectionAxis::Vertical);
        return e;
    }*/

}
