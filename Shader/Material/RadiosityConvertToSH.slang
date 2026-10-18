// Radiosity [C] ConvertToSH —— 把 [B] SpatialFilter 写的 FilteredTraceRadianceAtlas(每探针
// 16 根半球采样, 已做过 4 邻域空间滤波)投影成 1 阶(4 系数)球谐, 写进 ProbeSH R/G/B 三张
// 图集。按探针 dispatch(每页 32×32 个探针, 不是 128×128 个 texel)。光线方向必须跟 [A] 用
// 完全相同的公式重新生成——两边独立复用
// RadiosityProbeSampling.slang, 保证一致。跟 UE LumenRadiosityConvertToSH(LumenRadiosity.usf)
// 对照, 见 Document/Lumen_Radiosity_Plan.md §3[C]。
#include "../Common/Binding.slang"
#include "../Common/CardTexelDecode.slang"
#include "../Common/MonteCarlo.slang"
#include "../Common/RadiosityProbeSampling.slang"
#include "../Common/SHCommon.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(0, 2)]]
Texture2D<float4> CardNormalAtlas;

[[vk::binding(1, 2)]]
Texture2D<float> CardDepthAtlas;

[[vk::binding(2, 2)]]
Texture2D<float4> FilteredTraceRadianceAtlas;

[[vk::binding(3, 2)]]
[format("rgba16f")]
RWTexture2D<float4> ProbeSHRAtlas;

[[vk::binding(4, 2)]]
[format("rgba16f")]
RWTexture2D<float4> ProbeSHGAtlas;

[[vk::binding(5, 2)]]
[format("rgba16f")]
RWTexture2D<float4> ProbeSHBAtlas;

[[vk::binding(6, 2)]]
Texture2D<float4> BlueNoiseVec2Texture;

// 跟 RadiosityTraceConstants(RadiosityTrace.slang)同一套页身份字段, 这个 pass 不追踪,
// 不需要 GDF 相关的部分
struct RadiosityConvertToSHConstants
{
    float cardCenterX, cardCenterY, cardCenterZ;
    float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
    uint directionIndex;
    uint packedAddress; // mCardPageTable 打包值, 用 decodeCardPageAddress() 解出像素原点+实际尺寸
    uint pageIndexX, pageIndexY;
    uint pageCountX, pageCountY;
    uint instanceIndex;
    uint temporalIndex; // 跟 Trace 同一个值
};
[[vk::push_constant]]
RadiosityConvertToSHConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    CardPageRegion pageRegion = decodeCardPageAddress(PC.packedAddress);
    uint2 probesInPage = uint2(pageRegion.pixelExtent) / 4u;

    uint2 probeCoord = groupId.xy * 8 + localId.xy;
    if (probeCoord.x >= probesInPage.x || probeCoord.y >= probesInPage.y)
        return;

    uint2 probeAtlasCoord = uint2(pageRegion.pixelOrigin) / 4 + probeCoord;

    uint temporalIndex = PC.temporalIndex;
    uint2 probeJitter = getRadiosityProbeJitter(temporalIndex);
    uint2 probeTexel = probeCoord * 4 + probeJitter;

    CardTexelInfo probeInfo = decodeCardTexel(
        probeTexel,
        uint2(PC.pageIndexX, PC.pageIndexY),
        uint2(PC.pageCountX, PC.pageCountY),
        uint2(pageRegion.pixelOrigin),
        uint2(pageRegion.pixelExtent),
        PC.directionIndex,
        float3(PC.cardCenterX, PC.cardCenterY, PC.cardCenterZ),
        float3(PC.cardHalfExtentsX, PC.cardHalfExtentsY, PC.cardHalfExtentsZ),
        PC.instanceIndex,
        CardDepthAtlas,
        CardNormalAtlas);

    if (!probeInfo.valid)
    {
        ProbeSHRAtlas[probeAtlasCoord] = float4(0, 0, 0, 0);
        ProbeSHGAtlas[probeAtlasCoord] = float4(0, 0, 0, 0);
        ProbeSHBAtlas[probeAtlasCoord] = float4(0, 0, 0, 0);
        return;
    }

    uint2 probeGlobalCoord = probeCoord + uint2(pageRegion.pixelOrigin) / 4;
    float2 probeTexelCenter = getRadiosityProbeTexelCenter(BlueNoiseVec2Texture, temporalIndex, probeGlobalCoord);
    float3x3 tangentBasis = getTangentBasisFrisvad(probeInfo.normalWS);

    float4 shR = float4(0, 0, 0, 0);
    float4 shG = float4(0, 0, 0, 0);
    float4 shB = float4(0, 0, 0, 0);

    int2 pageOrigin = pageRegion.pixelOrigin;
    for (uint y = 0; y < 4; ++y)
    {
        for (uint x = 0; x < 4; ++x)
        {
            uint2 rayTexel = uint2(x, y);
            float2 probeUV = (float2(rayTexel) + probeTexelCenter) / 4.0;

            // 必须跟 [A] Trace 完全相同的 UniformSampleHemisphere + Frisvad 切线基, 否则这里
            // 重新生成的方向跟 TraceRadianceAtlas 里那个 texel 实际打出去的光线方向对不上
            float4 hemisphereSample = uniformSampleHemisphere(probeUV);
            float3 worldRayDir = mul(hemisphereSample.xyz, tangentBasis);
            float pdf = hemisphereSample.w;

            int2 traceTexelCoord = pageOrigin + int2(probeCoord * 4 + rayTexel);
            float3 radiance = FilteredTraceRadianceAtlas[traceTexelCoord].rgb;

            float4 basis = shBasisFunction(worldRayDir);
            float3 weight = radiance / pdf;
            shR += basis * weight.r;
            shG += basis * weight.g;
            shB += basis * weight.b;
        }
    }

    ProbeSHRAtlas[probeAtlasCoord] = shR / 16.0;
    ProbeSHGAtlas[probeAtlasCoord] = shG / 16.0;
    ProbeSHBAtlas[probeAtlasCoord] = shB / 16.0;
}
