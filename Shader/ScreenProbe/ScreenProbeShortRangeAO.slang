#include "../Common/Binding.slang"
#include "ScreenProbeShortRangeAOCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float> DownsampledSceneDepth;

[[vk::binding(1, 2)]]
Texture2D<float4> DownsampledWorldNormal;

[[vk::binding(2, 2)]]
Texture2D<float2> SceneHZB;

[[vk::binding(3, 2)]]
SamplerState HZBPointClampSampler;

[[vk::binding(4, 2)]]
Texture2D<float4> BlueNoiseVec2Texture;

[[vk::binding(5, 2)]]
[format("r32ui")]
RWTexture2D<uint> ShortRangeAO;

struct ScreenProbeShortRangeAOPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint2 downsampledViewExtent;
    float2 screenToHZBUVScale;
};
[[vk::push_constant]]
ScreenProbeShortRangeAOPushConstants PC;

float3x3 buildRotationMatrix(float3 fromDirection, float3 toDirection)
{
    float e = dot(fromDirection, toDirection);
    float f = abs(e);
    if (f > 1.0 - 0.0003)
        return float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);

    float3 v = cross(fromDirection, toDirection);
    float h = 1.0 / (1.0 + e);
    float hvx = h * v.x;
    float hvz = h * v.z;
    float hvxy = hvx * v.y;
    float hvxz = hvx * v.z;
    float hvyz = hvz * v.y;

    return float3x3(
        e + hvx * v.x, hvxy - v.z, hvxz + v.y,
        hvxy + v.z, e + h * v.y * v.y, hvyz - v.x,
        hvxz - v.y, hvyz + v.x, e + hvz * v.z);
}

float sampleHZBLinearDepth(float2 screenUV)
{
    float2 hzbUV = screenUV * PC.screenToHZBUVScale;
    float deviceZ = SceneHZB.SampleLevel(HZBPointClampSampler, hzbUV, 0.0).r;
    // deviceZ 为 0 的天空得到极大的线性深度, 由前景剔除自动收敛到不遮挡
    return screenProbeLinearDepth(deviceZ, uViewData);
}

void updateOccludedHorizonForStep(
    float2 sampleUV,
    float3 viewSpacePosition,
    float3 viewVector,
    float invForegroundDistance,
    float lowHorizonCos,
    inout float horizonCos)
{
    float sampleDepth = sampleHZBLinearDepth(sampleUV);
    float3 sampleViewPos = screenProbeViewPositionAtDepth(sampleUV, sampleDepth, uViewData);
    float3 sampleDelta = sampleViewPos - viewSpacePosition;
    float sampleDist = length(sampleDelta);
    float3 toSample = sampleDelta / max(sampleDist, 1e-8);

    float newHorizonCos = dot(toSample, viewVector);
    newHorizonCos = lerp(newHorizonCos, lowHorizonCos,
        saturate(pow(abs(sampleDelta.z) * invForegroundDistance, kShortRangeAOForegroundSampleRejectPower)));
    horizonCos = max(horizonCos, newHorizonCos);
}

void calculateAOHorizonSearch(
    uint2 screenCoord,
    float2 screenUV,
    float sceneDepth,
    float3 worldNormal,
    out float outAO,
    out float3 outBentNormal)
{
    float invForegroundDistance = 1.0 / (sceneDepth * kShortRangeAOForegroundSampleRejectDistanceFraction);

    float3 viewSpacePosition = screenProbeViewPositionAtDepth(screenUV, sceneDepth, uViewData);
    float distanceToCamera = length(viewSpacePosition);
    float3 viewVector = -viewSpacePosition / max(distanceToCamera, 1e-8);
    float3 viewNormal = mul(worldNormal, uViewData.worldToViewRot);

    float2 uniformRandom = shortRangeAOBlueNoiseVec2(BlueNoiseVec2Texture, screenCoord, screenProbeFrameIndex());
    float2 maxScreenFractionForAO = shortRangeAOMaxScreenFractionForAO(PC.viewExtent);

    float visibility = 0.0;
    float3 viewBentNormal = float3(0.0, 0.0, 0.0);
    float correction = 0.0;

    for (uint sliceIndex = 0; sliceIndex < kShortRangeAOSliceCount; ++sliceIndex)
    {
        float sliceFraction = (float(sliceIndex) + uniformRandom.x) / float(kShortRangeAOSliceCount);
        float phi = sliceFraction * PI;
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);
        float3 sliceDirection = float3(cosPhi, sinPhi, 0.0);

        float3 orthogonalSliceDirection = sliceDirection - dot(sliceDirection, viewVector) * viewVector;
        float3 axisVector = normalize(cross(orthogonalSliceDirection, viewVector));
        float3 projectedNormal = viewNormal - axisVector * dot(viewNormal, axisVector);
        float signNorm = sign(dot(orthogonalSliceDirection, projectedNormal));
        float projectedNormalLength = length(projectedNormal);
        float recipProjectedNormalLength = 1.0 / max(projectedNormalLength, 1e-6);
        float cosNormal = saturate(dot(projectedNormal, viewVector) * recipProjectedNormalLength);
        float normalAngle = signNorm * acos(cosNormal);
        float sinNormal = sin(normalAngle);

        float lowHorizonCos0 = cos(normalAngle + 0.5 * PI);
        float lowHorizonCos1 = cos(normalAngle - 0.5 * PI);
        float horizonCos0 = lowHorizonCos0;
        float horizonCos1 = lowHorizonCos1;

        for (uint stepIndex = 0; stepIndex < kShortRangeAOStepsPerSlice; ++stepIndex)
        {
            float stepFraction = (float(stepIndex) + uniformRandom.y) / float(kShortRangeAOStepsPerSlice);
            float2 stepRadius = stepFraction * stepFraction * maxScreenFractionForAO;
            // UV 的 y 轴向下, 所以切片方向的 y 分量取反
            float2 sampleOffset = (stepRadius + uViewData.resolution.zw) * float2(sliceDirection.x, -sliceDirection.y);

            float2 sampleUV0 = screenUV + sampleOffset;
            if (all(sampleUV0 >= 0.0) && all(sampleUV0 <= 1.0))
            {
                updateOccludedHorizonForStep(sampleUV0, viewSpacePosition, viewVector, invForegroundDistance, lowHorizonCos0, horizonCos0);
            }

            float2 sampleUV1 = screenUV - sampleOffset;
            if (all(sampleUV1 >= 0.0) && all(sampleUV1 <= 1.0))
            {
                updateOccludedHorizonForStep(sampleUV1, viewSpacePosition, viewVector, invForegroundDistance, lowHorizonCos1, horizonCos1);
            }
        }

        float h0 = -acos(clamp(horizonCos1, -1.0, 1.0));
        float h1 = acos(clamp(horizonCos0, -1.0, 1.0));

        float integralArc0 = (cosNormal + 2.0 * h0 * sinNormal - cos(2.0 * h0 - normalAngle)) / 4.0;
        float integralArc1 = (cosNormal + 2.0 * h1 * sinNormal - cos(2.0 * h1 - normalAngle)) / 4.0;
        float sliceVisibility = projectedNormalLength * (integralArc0 + integralArc1);

        float t0 = (6.0 * sin(h0 - normalAngle) - sin(3.0 * h0 - normalAngle)
            + 6.0 * sin(h1 - normalAngle) - sin(3.0 * h1 - normalAngle)
            + 16.0 * sinNormal - 3.0 * (sin(h0 + normalAngle) + sin(h1 + normalAngle))) / 12.0;
        float t1 = (-cos(3.0 * h0 - normalAngle) - cos(3.0 * h1 - normalAngle)
            + 8.0 * cos(normalAngle) - 3.0 * (cos(h0 + normalAngle) + cos(h1 + normalAngle))) / 12.0;
        // 朝向相机的轴是 +Z(UE 是 -Z), 所以 z 分量用 +t1
        float3 localBentNormal = float3(sliceDirection.x * t0, sliceDirection.y * t0, t1);
        // 屏幕中心处朝向相机的方向是 +Z(UE 是 -Z)
        localBentNormal = mul(buildRotationMatrix(float3(0.0, 0.0, 1.0), viewVector), localBentNormal) * projectedNormalLength;
        viewBentNormal += localBentNormal;

        visibility += sliceVisibility;
        correction += projectedNormalLength * (normalAngle * sinNormal + cosNormal);
    }

    visibility = max(visibility / max(correction, 1e-6), 0.03);

    outBentNormal = mul(uViewData.worldToViewRot, normalize(viewBentNormal)) * visibility;
    outAO = visibility;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 halfResCoord = dispatchThreadID.xy;
    if (any(halfResCoord >= PC.downsampledViewExtent))
        return;

    float sceneDepth = DownsampledSceneDepth[halfResCoord];
    if (sceneDepth <= 0.0)
    {
        ShortRangeAO[halfResCoord] = getShortRangeAOIdentityPacked();
        return;
    }

    float3 worldNormal = normalize(DownsampledWorldNormal[halfResCoord].xyz * 2.0 - 1.0);

    uint2 downsampledGlobalCoord = halfResCoord + PC.viewRectMin / kShortRangeAODownsampleFactor;
    uint2 jitter = getShortRangeAODownsampleJitter(downsampledGlobalCoord, screenProbeFrameIndex());
    uint2 fullResCoord = downsampledGlobalCoord * kShortRangeAODownsampleFactor + jitter;
    float2 screenUV = (float2(fullResCoord) + 0.5) * uViewData.resolution.zw;

    float ao;
    float3 bentNormal;
    calculateAOHorizonSearch(fullResCoord, screenUV, sceneDepth, worldNormal, ao, bentNormal);

    ShortRangeAO[halfResCoord] = packScreenBentNormal(bentNormal);
}
