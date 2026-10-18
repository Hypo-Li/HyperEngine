// M7.2(Lighting PDF)历史资源写入: 把这一帧空间滤波后的探针辐射度(ScreenProbeRadianceFiltered)
// 和探针自身线性深度(ScreenProbeSceneDepth)原样拷进单缓冲持久化历史, 供下一帧的
// ScreenProbeLightingPDF 读——跟 ScreenProbeSceneDepthHistoryCopy.slang 同一个模式, 见
// Document/Lumen_ScreenProbeGather_Plan.md §9.2。调用方必须排在这一帧唯一的读者
// (ScreenProbeImportanceSamplingPass 的 Lighting PDF pass, 排在 Placement 之后、Filtering 之前)
// 之后调用, 单缓冲不做 ping-pong, 读写顺序由调用位置保证。

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
Texture2D<float4> ScreenProbeRadiance;

[[vk::binding(2, 2)]]
[format("r32f")]
WTexture2D<float> ScreenProbeLightingHistorySceneDepth;

[[vk::binding(3, 2)]]
[format("r11f_g11f_b10f")]
WTexture2D<float4> ScreenProbeLightingHistoryRadiance;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint radianceWidth, radianceHeight;
    ScreenProbeLightingHistoryRadiance.GetDimensions(radianceWidth, radianceHeight);
    if (dispatchThreadID.x < radianceWidth && dispatchThreadID.y < radianceHeight)
        ScreenProbeLightingHistoryRadiance.Store(dispatchThreadID.xy, ScreenProbeRadiance[dispatchThreadID.xy]);

    uint depthWidth, depthHeight;
    ScreenProbeLightingHistorySceneDepth.GetDimensions(depthWidth, depthHeight);
    if (dispatchThreadID.x < depthWidth && dispatchThreadID.y < depthHeight)
        ScreenProbeLightingHistorySceneDepth.Store(dispatchThreadID.xy, ScreenProbeSceneDepth[dispatchThreadID.xy]);
}
