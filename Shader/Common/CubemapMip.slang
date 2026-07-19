#include "Cubemap.slang"

// 通用的"生成一级 cubemap mip"compute shader: 每次 dispatch 只读上一级 mip、只写当前级 mip,
// 不绑定整条 mip 链——调用方对每一级 mip 各开一个独立的 RenderGraph compute pass,读写关系交给
// RenderGraph 按 builder.sample/builder.storage 的声明自动推导 barrier,不需要手动管理同步,
// Vulkan/WebGPU 都能用同一份代码走通

[[vk::binding(0, 2)]]
SamplerState LinearSampler;

[[vk::binding(1, 2)]]
TextureCube<float4> SrcMip;

[[vk::binding(2, 2)]]
[format("rgba16f")]
WTexture2DArray<float4> DstMip;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height, elements;
    DstMip.GetDimensions(width, height, elements);

    if (id.x >= width || id.y >= height || id.z >= 6)
        return;

    float2 uv = (id.xy + 0.5) / float2(width, height);
    float3 dir = faceUVToCubemapDir(id.z, uv);
    float4 color = SrcMip.SampleLevel(LinearSampler, dir, 0.0);
    DstMip.Store(id, color);
}
