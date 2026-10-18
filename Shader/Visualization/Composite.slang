[[vk::binding(0, 2)]]
Texture2DMS<float4> VisualizationColor;

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

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    uint width, height, sampleCount;
    VisualizationColor.GetDimensions(width, height, sampleCount);

    int2 coord = int2(svPosition.xy);
    float4 sum = float4(0, 0, 0, 0);
    for (uint i = 0; i < sampleCount; i++)
    {
        sum += VisualizationColor.Load(coord, i);
    }
    return sum / float(sampleCount);
}
