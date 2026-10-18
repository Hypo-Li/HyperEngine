[[vk::binding(0, 0)]]
StructuredBuffer<uint> HistogramScatter;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float> Histogram;

struct HistogramConvertConstants
{
    uint2 resolution;
    uint padding0;
    uint padding1;
};
[[vk::push_constant]]
HistogramConvertConstants PC;

static constexpr uint kHistogramBucketCount = 64;
static constexpr uint kFixedPointFractionBits = 19;
static constexpr float kInverseFixedPointScale = 1.0 / float(1u << kFixedPointFractionBits);
static constexpr float kHighWordScale = float(1u << (32u - kFixedPointFractionBits));

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint bucket = dispatchThreadID.x;
    if (bucket >= kHistogramBucketCount)
        return;

    uint low = HistogramScatter[bucket * 2u];
    uint high = HistogramScatter[bucket * 2u + 1u];
    float weight = float(high) * kHighWordScale + float(low) * kInverseFixedPointScale;
    float pixelCount = max(float(PC.resolution.x) * float(PC.resolution.y), 1.0);
    Histogram[bucket] = weight / pixelCount;
}
