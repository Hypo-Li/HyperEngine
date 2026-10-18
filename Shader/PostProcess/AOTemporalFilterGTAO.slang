#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/GBufferEncoding.slang"

[[vk::binding(0, 2)]]
SamplerState PointClampSampler;

[[vk::binding(1, 2)]]
[UnfilterableFloat]
Texture2D<float4> Velocity;

[[vk::binding(2, 2)]]
Texture2D<float> AOFilteredImage;

[[vk::binding(3, 2)]]
Texture2D<float> AOHistoryRead;

[[vk::binding(4, 2)]]
[format("r8")]
WTexture2D<float> AOHistoryWrite;

struct AOTemporalFilterConstants
{
    uint historyValid;
    float temporalBlendWeight;
};
[[vk::push_constant]]
AOTemporalFilterConstants PC;

float readHistoryClamp(
    float2 historyUV,
    float clampMin,
    float clampMax,
    uint width,
    uint height)
{
    float2 historyPixel = historyUV * float2(width, height) - 0.5;
    int2 baseCoord = int2(floor(historyPixel));
    float2 bilinearWeight = frac(historyPixel);
    int2 maxCoord = int2(width - 1u, height - 1u);

    float h00 = clamp(AOHistoryRead[clamp(baseCoord, int2(0, 0), maxCoord)], clampMin, clampMax);
    float h10 = clamp(AOHistoryRead[clamp(baseCoord + int2(1, 0), int2(0, 0), maxCoord)], clampMin, clampMax);
    float h01 = clamp(AOHistoryRead[clamp(baseCoord + int2(0, 1), int2(0, 0), maxCoord)], clampMin, clampMax);
    float h11 = clamp(AOHistoryRead[clamp(baseCoord + int2(1, 1), int2(0, 0), maxCoord)], clampMin, clampMax);

    float top = lerp(h00, h10, bilinearWeight.x);
    float bottom = lerp(h01, h11, bilinearWeight.x);
    return lerp(top, bottom, bilinearWeight.y);
}

// GTAO owns a temporal history independent from the main TAA history. Clamp
// every point tap before manual bilinear interpolation, then reject displaced
// history when the motion at the destination disagrees with the source motion.
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    AOHistoryWrite.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    int2 coord = int2(dispatchThreadID.xy);
    float centerFiltered = AOFilteredImage[coord];

    if (PC.historyValid == 0)
    {
        AOHistoryWrite.Store(coord, centerFiltered);
        return;
    }

    float2 currentPixelUV = (float2(coord) + 0.5) / float2(width, height);
    float2 velocityNDC = decodeVelocity(Velocity[coord]).xy;
    if (!all(abs(velocityNDC) < float2(4.0)))
    {
        AOHistoryWrite.Store(coord, centerFiltered);
        return;
    }

    float2 velocityUV = velocityNDC * float2(0.5, -0.5);
    float2 historyUV = currentPixelUV - velocityUV;
    if (any(historyUV < 0.0) || any(historyUV > 1.0))
    {
        AOHistoryWrite.Store(coord, centerFiltered);
        return;
    }

    float velocityMagnitude = saturate(length(velocityUV) * 100.0);
    float rangeValue = lerp(0.1, 0.0, velocityMagnitude);
    float clampMin = centerFiltered - rangeValue;
    float clampMax = centerFiltered + rangeValue;
    float historyAtReprojectedUV = readHistoryClamp(
        historyUV, clampMin, clampMax, width, height);
    float historyAtCurrentUV = clamp(AOHistoryRead[coord], clampMin, clampMax);

    float2 destinationVelocityNDC = decodeVelocity(Velocity.SampleLevel(PointClampSampler, historyUV, 0.0)).xy;
    float velocityAgreement = 0.0;
    if (all(abs(destinationVelocityNDC) < float2(4.0)))
    {
        float2 destinationVelocityUV = destinationVelocityNDC * float2(0.5, -0.5);
        float2 velocityDifference = velocityUV - destinationVelocityUV;
        velocityAgreement = 1.0 - saturate(abs(velocityDifference.x + velocityDifference.y) * 100.0);
    }

    float historyAO = lerp(historyAtCurrentUV, historyAtReprojectedUV, velocityAgreement);
    float resolved = lerp(historyAO, centerFiltered, PC.temporalBlendWeight);
    AOHistoryWrite.Store(coord, resolved);
}
