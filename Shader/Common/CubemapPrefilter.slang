#include "Cubemap.slang"

// 通用的 GGX 镜面反射 prefilter: 读一整条已经建好 mip 链的源 cubemap(不同粗糙度用不同 mip 做
// importance sampling 降噪), 写目标 prefilter cubemap 的某一级 mip(每一级对应一个粗糙度)。
// 调用方对每一级目标 mip 各开一个独立的 RenderGraph compute pass, 用 immediate data(push
// constant)传当前处理的 mipLevel/sampleCount, 读写关系交给 RenderGraph 按 builder.sample/
// builder.storage 的声明自动推导 barrier

[[vk::binding(0, 2)]]
SamplerState LinearSampler;

[[vk::binding(1, 2)]]
TextureCube<float4> SrcCubemap;

[[vk::binding(2, 2)]]
[format("rgba16f")]
WTexture2DArray<float4> DstPrefilterMip;

struct PrefilterPushConstants
{
    uint mipLevel;
    uint sampleCount;
    uint sourceResolution;
    uint sourceMipCount;
    uint prefilterMipCount;
};
[[vk::push_constant]]
PrefilterPushConstants PC;

static constexpr float PI = 3.14159265358979323846;

float2 hammersley(uint index, float sampleCount)
{
    float invNumSamples = 1.0 / sampleCount;
    const float tof = 0.5 / float(0x80000000u);
    uint bits = index;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float2(float(index) * invNumSamples, float(bits) * tof);
}

float3 importanceSampleGGX(float2 u, float a)
{
    float phi = 2.0 * PI * u.x;
    // NOTE: (aa-1) == (a-1)(a+1) 数值精度更好
    float cosTheta2 = (1.0 - u.y) / (1.0 + (a + 1.0) * ((a - 1.0) * u.y));
    float cosTheta = sqrt(cosTheta2);
    float sinTheta = sqrt(1.0 - cosTheta2);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float D_GGX(float NoH, float a)
{
    float f = (a - 1.0) * ((a + 1.0) * (NoH * NoH)) + 1.0;
    return (a * a) / (PI * f * f);
}

float lodToPerceptualRoughness(float lod)
{
    const float a = 2.0;
    const float b = -1.0;
    return (lod != 0.0) ? clamp((sqrt(a * a + 4.0 * b * lod) - a) / (2.0 * b), 0.0, 1.0) : 0.0;
}

float log4(float x)
{
    return log2(x) * 0.5;
}

float random(float2 w)
{
    const float3 m = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(m.z * frac(dot(w, m.xy)));
}

float3 prefilterEnvMap(float roughness, float3 N, float2 pixelPos)
{
    float a = roughness * roughness;

    // 每个像素独立随机旋转切线系一个角度, 让相邻像素的采样点不完全相关, 减少条带感;
    // 不用矩阵类型/mul() 是为了避开 HLSL/WGSL 行主序 vs GLSL 列主序的构造语义差异, 直接手写
    // 基向量的线性组合, 语义上和原始 GLSL 版本(tangentSpace(N) * R, 列向量右乘)完全等价
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t0 = normalize(cross(up, N));
    float3 t1 = cross(N, t0);

    float r = 2.0 * PI * random(pixelPos + 0.5);
    float c = cos(r), s = sin(r);
    float3 t0r = c * t0 + s * t1;
    float3 t1r = -s * t0 + c * t1;

    const float omegaP = (4.0 * PI) / float(6u * PC.sourceResolution * PC.sourceResolution);

    float3 filteredColor = float3(0.0, 0.0, 0.0);
    float weight = 0.0;
    for (uint i = 0u; i < PC.sampleCount; ++i)
    {
        float2 u = hammersley(i, float(PC.sampleCount));
        float3 H = importanceSampleGGX(u, a);
        float NoH = H.z;
        float NoH2 = H.z * H.z;
        float NoL = 2.0 * NoH2 - 1.0;
        float3 L = float3(2.0 * NoH * H.xy, NoL);

        if (NoL > 0.0)
        {
            float pdf = max(D_GGX(NoH, a) * 0.25, 1e-6);
            float omegaS = 1.0 / (float(PC.sampleCount) * pdf);
            const float mipBias = 0.9;
            float l = log4(omegaS) - log4(omegaP) + mipBias;
            float mip = roughness == 0.0 ? 0.0 : clamp(l, 0.0, float(PC.sourceMipCount - 1u));

            float3 dirWS = L.x * t0r + L.y * t1r + L.z * N;
            float3 sampleColor = SrcCubemap.SampleLevel(LinearSampler, dirWS, mip).rgb;
            filteredColor += sampleColor * NoL;
            weight += NoL;
        }
    }
    return weight > 0.0 ? filteredColor / weight : float3(0.0, 0.0, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height, elements;
    DstPrefilterMip.GetDimensions(width, height, elements);

    if (id.x >= width || id.y >= height || id.z >= 6)
        return;

    float2 uv = (id.xy + 0.5) / float2(width, height);
    float3 dir = faceUVToCubemapDir(id.z, uv);

    float lod = PC.prefilterMipCount > 1u
        ? clamp(float(PC.mipLevel) / float(PC.prefilterMipCount - 1u), 0.0, 1.0)
        : 0.0;
    float roughness = lodToPerceptualRoughness(lod);

    float3 color = prefilterEnvMap(roughness, dir, float2(id.xy));
    DstPrefilterMip.Store(id, float4(color, 1.0));
}
