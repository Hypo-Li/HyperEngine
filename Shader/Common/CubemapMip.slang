#include "Cubemap.slang"

#define MAX_MIP_LEVELS 12

[[vk::binding(0, 2)]]
SamplerCube CubemapMipTextures[MAX_MIP_LEVELS];

[[vk::binding(1, 2)]]
RWTexture2DArray<float4> CubemapMipImages[MAX_MIP_LEVELS];

// should start at 1
[[vk::push_constant]]
uint kCurrentMip;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height, elements;
    CubemapMipImages[kCurrentMip].GetDimensions(width, height, elements);

    if (id.x >= width || id.y >= height || id.z >= 6)
        return;

    float2 uv = (id.xy + 0.5) / float2(width, height);
    float3 dir = faceUVToCubemapDir(id.z, uv);
    float4 color = CubemapMipTextures[kCurrentMip - 1].SampleLevel(dir, 0.0);
    CubemapMipImages[kCurrentMip][id] = color;
}