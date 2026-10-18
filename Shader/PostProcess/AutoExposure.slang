#include "../Common/Binding.slang"

[[vk::binding(0, 2)]]
StructuredBuffer<float> Histogram;

// x: adapted exposure scale, y: target exposure scale,
// z: measured average scene luminance, w: PreExposure used for this frame.
[[vk::binding(1, 2)]]
RWStructuredBuffer<float4> AutoExposureData;

struct AutoExposureConstants
{
    float histogramScale;
    float histogramBias;
    float exposureLowPercent;
    float exposureHighPercent;

    float minAverageLuminance;
    float maxAverageLuminance;
    float exposureSpeedUp;
    float exposureSpeedDown;

    float exponentialUpM;
    float exponentialDownM;
    float startDistance;
    float preExposure;

    float exposureCompensationScale;
    float manualExposureScale;
    uint exposureMode;
    uint forceTarget;
};
[[vk::push_constant]]
AutoExposureConstants PC;

static constexpr uint kHistogramBucketCount = 64;
static constexpr float kMiddleGrey = 0.18;

float histogramBucketToLogLuminance(uint bucket)
{
    float position = float(bucket) / float(kHistogramBucketCount - 1u);
    return (position - PC.histogramBias) / PC.histogramScale;
}

float computeAverageSceneLuminance()
{
    float histogramSum = 0.0;
    for (uint i = 0u; i < kHistogramBucketCount; ++i)
        histogramSum += Histogram[i];

    float lowRemaining = histogramSum * PC.exposureLowPercent;
    float highRemaining = histogramSum * PC.exposureHighPercent;
    float weightedLogLuminance = 0.0;
    float retainedWeight = 0.0;

    for (uint i = 0u; i < kHistogramBucketCount; ++i)
    {
        float weight = Histogram[i];

        float lowRemoval = min(weight, lowRemaining);
        weight -= lowRemoval;
        lowRemaining -= lowRemoval;
        highRemaining -= lowRemoval;

        weight = min(weight, max(highRemaining, 0.0));
        highRemaining -= weight;

        weightedLogLuminance += histogramBucketToLogLuminance(i) * weight;
        retainedWeight += weight;
    }

    if (retainedWeight <= 0.000001)
        return PC.minAverageLuminance;
    return exp2(weightedLogLuminance / retainedWeight);
}

float adaptExposureDenominator(float current, float target)
{
    float logCurrent = log2(max(current, 0.0001));
    float logTarget = log2(max(target, 0.0001));
    float difference = logTarget - logCurrent;

    float speed = difference > 0.0 ? PC.exposureSpeedUp : PC.exposureSpeedDown;
    float exponentialM = difference > 0.0 ? PC.exponentialUpM : PC.exponentialDownM;

    float deltaTime = clamp(uFrameData.deltaTime, 0.0, 0.1);
    float exponentialFactor = (1.0 - exp2(-deltaTime * speed)) * exponentialM;
    float exponentialResult = lerp(logCurrent, logTarget, exponentialFactor);

    float linearStep = deltaTime * speed;
    float linearResult = logCurrent + clamp(difference, -linearStep, linearStep);
    float adapted = abs(difference) > PC.startDistance ? linearResult : exponentialResult;
    return exp2(adapted);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeMain()
{
    float preExposure = max(PC.preExposure, 0.0001);

    // ExposureMode::Manual == 0, ExposureMode::Automatic == 1.
    if (PC.exposureMode == 0u)
    {
        AutoExposureData[0] = float4(
            PC.manualExposureScale,
            PC.manualExposureScale,
            0.0,
            preExposure);
        return;
    }

    float averageSceneLuminance = computeAverageSceneLuminance();
    float targetAverageLuminance = clamp(
        averageSceneLuminance,
        PC.minAverageLuminance,
        PC.maxAverageLuminance);
    float targetExposureDenominator = targetAverageLuminance / kMiddleGrey;
    float targetExposureScale = PC.exposureCompensationScale /
        max(targetExposureDenominator, 0.0001);

    float adaptedExposureDenominator = targetExposureDenominator;
    if (PC.forceTarget == 0u)
    {
        // Adaptation stays entirely on the GPU and therefore uses the immediately previous
        // adapted value. PC.preExposure is a delayed CPU readback used only to normalize the
        // current frame's pre-exposed color domain.
        float oldExposureScale = max(AutoExposureData[0].x, 0.0001);
        float oldExposureDenominator = PC.exposureCompensationScale / oldExposureScale;
        adaptedExposureDenominator = adaptExposureDenominator(
            oldExposureDenominator,
            targetExposureDenominator);
    }

    adaptedExposureDenominator = clamp(
        adaptedExposureDenominator,
        PC.minAverageLuminance / kMiddleGrey,
        PC.maxAverageLuminance / kMiddleGrey);
    float adaptedExposureScale = PC.exposureCompensationScale /
        max(adaptedExposureDenominator, 0.0001);

    AutoExposureData[0] = float4(
        adaptedExposureScale,
        targetExposureScale,
        averageSceneLuminance,
        preExposure);
}
