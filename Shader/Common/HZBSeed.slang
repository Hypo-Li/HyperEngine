[[vk::binding(0, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(1, 2)]]
[format("rg32f")]
WTexture2D<float2> SceneHZBMip0;

// 每个输出 texel 代表 2x2 全分辨率 SceneDepth footprint 里的两个极值(reversed-Z):
// R = Furthest(min, 这个 tile 里最远的表面)、G = Closest(max, 这个 tile 里最近的可能表面)。
// 屏幕空间追踪(HZBTrace.slang)用 Closest 做保守的空白跳过判定, AO 只用 Furthest。
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint dstWidth, dstHeight;
    SceneHZBMip0.GetDimensions(dstWidth, dstHeight);
    if (dispatchThreadID.x >= dstWidth || dispatchThreadID.y >= dstHeight)
        return;

    uint srcWidth, srcHeight;
    SceneDepth.GetDimensions(srcWidth, srcHeight);
    uint2 maxCoord = uint2(srcWidth - 1u, srcHeight - 1u);
    uint2 srcBase = dispatchThreadID.xy * 2u;

    float depth00 = SceneDepth[min(srcBase, maxCoord)];
    float depth10 = SceneDepth[min(srcBase + uint2(1u, 0u), maxCoord)];
    float depth01 = SceneDepth[min(srcBase + uint2(0u, 1u), maxCoord)];
    float depth11 = SceneDepth[min(srcBase + uint2(1u, 1u), maxCoord)];

    float furthest = min(min(depth00, depth10), min(depth01, depth11));
    float closest = max(max(depth00, depth10), max(depth01, depth11));
    SceneHZBMip0.Store(dispatchThreadID.xy, float2(furthest, closest));
}
