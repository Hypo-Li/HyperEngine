#pragma once

static constexpr float PI = 3.1415926535897932;
static constexpr float RAD_TO_DEG = 180.0 / PI;
static constexpr float DEG_TO_RAD = PI / 180.0;

__generic<typename T>
T pow2(T x) where T : IArithmetic
{
    return x * x;
}

float interleavedGradientNoise(float2 uv, float frameId)
{
    uv += frameId * (float2(47, 17) * 0.695);
    static constexpr float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

// 把 [0,1) 的均匀噪声重映射成三角分布(TPDF), 用于量化前的抖动: 相比均匀噪声, 三角分布抖动
// 不会引入额外的噪声方差, 是标准的抗 banding 手法(参考 UE PostProcessTonemap.usf 里同名用途的代码)
float uniformNoiseToTriangular(float uniformNoise)
{
    float np = uniformNoise * 2.0 - 1.0;
    float npSign = np < 0.0 ? -1.0 : 1.0;
    return npSign - npSign * sqrt(saturate(1.0 - abs(np)));
}
