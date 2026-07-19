#include "../Common/Cubemap.slang"

[[vk::binding(0, 2)]]
SamplerCube SrcCubemap;

[[vk::binding(1, 2)]]
RWStructuredBuffer<float4> DstSHCoeff;

static constexpr float kSHConstants[9] = {
    // L0 (1个系数)
    0.2820947918, // Y00: 0.5 * sqrt(1/π)

    // L1 (3个系数)
    -0.4886025119, // Y1-1: sqrt(3/(4π)) 
    0.4886025119,  // Y10
    -0.4886025119, // Y11

    // L2 (5个系数)
    1.0925484306,  // Y2-2: sqrt(15/(4π))
    -1.0925484306, // Y2-1
    0.3153915652,  // Y20: sqrt(5/(16π))
    -1.0925484306, // Y21
    0.5462742153    // Y22: sqrt(15/(16π))
};

static constexpr float PI = 3.14159265358979323846;
static constexpr float A0 = PI;
static constexpr float A1 = 2.0 * PI / 3.0;
static constexpr float A2 = PI / 4.0;

float areaElement(float u, float v)
{
    float invLen = rsqrt(1.0 + u * u + v * v);
    return atan2(u * v * invLen, invLen);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadId : SV_DispatchThreadID, uint id : SV_GroupID)
{
    static constexpr uint kFaceSize = 32u;
    static constexpr float kInvSize = 1.0 / float(kFaceSize);
    uint face = dispatchThreadId.z;
    uint x = dispatchThreadId.x;
    uint y = dispatchThreadId.y;
    float u = (x + 0.5) * kInvSize;
    float v = (y + 0.5) * kInvSize;
    float3 dir = faceUVToCubemapDir(face, float2(u, v));

    // 立体角权重（以 texel 边界计算）
    float u0 = ( (float(x)    ) * kInvSize ) * 2.0 - 1.0;
    float v0 = ( (float(y)    ) * kInvSize ) * 2.0 - 1.0;
    float u1 = ( (float(x) + 1) * kInvSize ) * 2.0 - 1.0;
    float v1 = ( (float(y) + 1) * kInvSize ) * 2.0 - 1.0;
    float solidAngle = 
          areaElement(u0, v0)
        - areaElement(u0, v1)
        - areaElement(u1, v0)
        + areaElement(u1, v1);
    
    float3 L = SrcCubemap.SampleLevel(dir, 4.0).rgb;

    // SH 基
    float x1 = dir.x, y1 = dir.y, z1 = dir.z;
    float basis[9];
    basis[0] = kSHConstants[0];
    basis[1] = kSHConstants[1] * y1;
    basis[2] = kSHConstants[2] * z1;
    basis[3] = kSHConstants[3] * x1;
    basis[4] = kSHConstants[4] * x1 * y1;
    basis[5] = kSHConstants[5] * y1 * z1;
    basis[6] = kSHConstants[6] * (3.0*z1*z1 - 1.0);
    basis[7] = kSHConstants[7] * x1 * z1;
    basis[8] = kSHConstants[8] * (x1*x1 - y1*y1);

    float Al[9] = float[9](A0, A1, A1, A1, A2, A2, A2, A2, A2);

    // 每线程贡献
    float3 contrib[9];
    for (int i = 0; i < 9; ++i)
    {
        contrib[i] = L * (basis[i] * solidAngle * Al[i]);
    }

    // 子组归约：每个子组对 9 个系数各自求和
    for (int i = 0; i < 9; ++i)
    {
        float3 sum = float3(
            WaveActiveSum(contrib[i].x),
            WaveActiveSum(contrib[i].y),
            WaveActiveSum(contrib[i].z)
        );

        // 仅子组代表写回，减少全局原子次数
        if (WaveIsFirstLane())
        {
            InterlockedAdd(DstSHCoeff[i].x, sum.x);
            InterlockedAdd(DstSHCoeff[i].y, sum.y);
            InterlockedAdd(DstSHCoeff[i].z, sum.z);
        }
    }
}