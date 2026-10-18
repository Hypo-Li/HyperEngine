[[vk::binding(0, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(1, 2)]]
[format("r32f")]
WTexture2D<float> SceneDepthHistory;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    SceneDepthHistory.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    SceneDepthHistory.Store(dispatchThreadID.xy, SceneDepth[dispatchThreadID.xy]);
}
