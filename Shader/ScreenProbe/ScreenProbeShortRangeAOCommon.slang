#pragma once
#include "../Common/Binding.slang"
#include "ScreenProbeCommon.slang"

static const uint kShortRangeAODownsampleFactor = 2u;
static const uint kShortRangeAOSliceCount = 2u;
static const uint kShortRangeAOStepsPerSlice = 3u;
static const float kShortRangeAOForegroundSampleRejectDistanceFraction = 0.3;
static const float kShortRangeAOForegroundSampleRejectPower = 1.0;
static const float kLumenMaxAOMultibounceAlbedo = 0.5;
static const float kShortRangeAOTemporalNeighborhoodClampScale = 1.0;

uint2 getShortRangeAODownsampleJitter(uint2 downsampledCoord, uint frameIndex)
{
    uint2 cellIndex = downsampledCoord % 2u;
    uint linearIndex = (cellIndex.x + cellIndex.y * 2u + frameIndex) % 4u;
    uint2 jitter;
    jitter.x = (linearIndex & 0x2u) != 0u ? 1u : 0u;
    jitter.y = (linearIndex & 0x1u) != 0u ? 0u : 1u;
    return jitter;
}

int2 shortRangeAODownsampleAnchor(int2 fullResCoord)
{
    return (fullResCoord - 1) / int(kShortRangeAODownsampleFactor);
}

uint packRGB111110(float3 rgb)
{
    uint r = uint(floor(saturate(rgb.r) * 2047.0 + 0.5));
    uint g = uint(floor(saturate(rgb.g) * 2047.0 + 0.5));
    uint b = uint(floor(saturate(rgb.b) * 1023.0 + 0.5));
    return (r << 21) | (g << 10) | b;
}

float3 unpackRGB111110(uint rgb)
{
    float r = float(rgb >> 21) * (1.0 / 2047.0);
    float g = float((rgb >> 10) & 2047u) * (1.0 / 2047.0);
    float b = float(rgb & 1023u) * (1.0 / 1023.0);
    return float3(r, g, b);
}

uint packScreenBentNormal(float3 bentNormal)
{
    return packRGB111110(bentNormal * 0.5 + 0.5);
}

float3 unpackScreenBentNormal(uint packedBentNormal)
{
    return unpackRGB111110(packedBentNormal) * 2.0 - 1.0;
}

uint getShortRangeAOIdentityPacked()
{
    return packScreenBentNormal(float3(0.0, 0.0, 1.0));
}

float2 shortRangeAOMaxScreenFractionForAO(uint2 viewExtent)
{
    return float2(kScreenProbeDownsampleFactor * 2u) / float2(viewExtent);
}

float shortRangeAONormalWeight(float3 sceneWorldNormal, float3 sampleWorldNormal)
{
    float angleBetweenNormals = acos(saturate(dot(sampleWorldNormal, sceneWorldNormal)));
    float normalWeight = 1.0 - saturate(angleBetweenNormals);
    return normalWeight * normalWeight;
}

float shortRangeAOBlueNoiseScalar(Texture2D<float4> blueNoiseVec2Texture, uint2 screenCoord, uint temporalIndex)
{
    return getRadiosityProbeTexelCenter(blueNoiseVec2Texture, temporalIndex + (kBlueNoiseVec2Dimensions.z - 1u) / 2u, screenCoord).x;
}

float2 shortRangeAOBlueNoiseVec2(Texture2D<float4> blueNoiseVec2Texture, uint2 screenCoord, uint temporalIndex)
{
    return getRadiosityProbeTexelCenter(blueNoiseVec2Texture, temporalIndex, screenCoord);
}
