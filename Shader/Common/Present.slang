#include "../Common/Binding.slang"

[[vk::binding(0, 2)]]
Texture2D<float4> SceneColor;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

[shader("vertex")]
float4 VertexMain(in uint vertexID : SV_VertexID) : SV_Position
{
    return float4(kPositions[vertexID], 0, 1);
}

float3 ACESFilm(float3 x)
 {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    
    // 公式: (x * (a * x + b)) / (x * (c * x + d) + e)
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float4 color = SceneColor[uint2(svPosition.xy)];
    return float4(pow(ACESFilm(color.rgb), float3(1.0f / 2.2f)), 1.0);
}
