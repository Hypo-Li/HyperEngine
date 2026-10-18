[[vk::binding(0, 0)]]
Texture2D<float4> SceneColor;

// Each of the 64 histogram buckets is represented by a low uint followed by a
// carry uint. This keeps the fixed-point accumulation portable to WebGPU, where
// 64-bit atomics and storage-texture atomics are not generally available.
[[vk::binding(1, 0)]]
RWStructuredBuffer<Atomic<uint>> HistogramScatter;

struct HistogramAtomicConstants
{
    uint2 resolution;
    float histogramScale;
    float histogramBias;
    float minLuminance;
    float oneOverPreExposure;
    uint padding0;
    uint padding1;
};
[[vk::push_constant]]
HistogramAtomicConstants PC;

static constexpr uint kHistogramBucketCount = 64;
static constexpr uint kFixedPointFractionBits = 19;
static constexpr float kFixedPointScale = float(1u << kFixedPointFractionBits);
// UE r.AutoExposure.LuminanceMethod 默认值 0(Uniform)——不是常被想当然认为的 Rec.709 感知权重
static constexpr float3 kLuminanceWeights = float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0);

groupshared Atomic<uint> gHistogram[64];

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeMain(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint groupIndex : SV_GroupIndex)
{
    if (groupIndex < kHistogramBucketCount)
        gHistogram[groupIndex].store(0u);
    GroupMemoryBarrierWithGroupSync();

    if (all(dispatchThreadID.xy < PC.resolution))
    {
        float3 sceneColor = max(SceneColor[dispatchThreadID.xy].rgb * PC.oneOverPreExposure, 0.0);
        float luminance = max(dot(sceneColor, kLuminanceWeights), PC.minLuminance);
        float histogramPosition = saturate(log2(luminance) * PC.histogramScale + PC.histogramBias);
        float continuousBucket = histogramPosition * float(kHistogramBucketCount - 1u);

        uint bucket0 = min(uint(continuousBucket), kHistogramBucketCount - 1u);
        uint bucket1 = min(bucket0 + 1u, kHistogramBucketCount - 1u);
        float weight1 = frac(continuousBucket);
        float weight0 = 1.0 - weight1;

        // Ignore the clamped-black bucket, matching UE's default black histogram
        // influence of zero. The upper neighbor still receives its interpolated weight.
        if (bucket0 == 0u)
            weight0 = 0.0;

        gHistogram[bucket0].add(uint(weight0 * kFixedPointScale));
        gHistogram[bucket1].add(uint(weight1 * kFixedPointScale));
    }

    GroupMemoryBarrierWithGroupSync();

    if (groupIndex < kHistogramBucketCount)
    {
        uint contribution = gHistogram[groupIndex].load();
        if (contribution != 0u)
        {
            uint lowIndex = groupIndex * 2u;
            uint previousLow = HistogramScatter[lowIndex].add(contribution);
            if (previousLow > 0xffffffffu - contribution)
                HistogramScatter[lowIndex + 1u].add(1u);
        }
    }
}
