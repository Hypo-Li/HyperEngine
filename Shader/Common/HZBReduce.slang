[[vk::binding(0, 2)]]
Texture2D<float2> SrcHZBMip;

[[vk::binding(1, 2)]]
[format("rg32f")]
WTexture2D<float2> DstHZBMip;

// Each source view exposes exactly one mip as level 0. Keeping the reduction on
// integer texel coordinates avoids filtering depth extrema while building the pyramid.
// R(Furthest)/G(Closest) 各自独立归约(min-of-min / max-of-max), 互不影响, 见 HZBSeed.slang。
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint dstWidth, dstHeight;
    DstHZBMip.GetDimensions(dstWidth, dstHeight);
    if (dispatchThreadID.x >= dstWidth || dispatchThreadID.y >= dstHeight)
        return;

    uint srcWidth, srcHeight;
    SrcHZBMip.GetDimensions(srcWidth, srcHeight);
    uint2 maxCoord = uint2(srcWidth - 1u, srcHeight - 1u);
    uint2 srcBase = dispatchThreadID.xy * 2u;

    float2 depth00 = SrcHZBMip[min(srcBase, maxCoord)];
    float2 depth10 = SrcHZBMip[min(srcBase + uint2(1u, 0u), maxCoord)];
    float2 depth01 = SrcHZBMip[min(srcBase + uint2(0u, 1u), maxCoord)];
    float2 depth11 = SrcHZBMip[min(srcBase + uint2(1u, 1u), maxCoord)];

    float furthest = min(min(depth00.r, depth10.r), min(depth01.r, depth11.r));
    float closest = max(max(depth00.g, depth10.g), max(depth01.g, depth11.g));
    DstHZBMip.Store(dispatchThreadID.xy, float2(furthest, closest));
}
