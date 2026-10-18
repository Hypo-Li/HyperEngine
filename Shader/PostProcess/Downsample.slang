[[vk::binding(0, 2)]]
Texture2D<float4> SceneColor;

struct DownsampleConstants
{
    uint2 inputResolution;
    uint2 outputResolution;
};
[[vk::push_constant]]
DownsampleConstants PC;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

struct VertexOutput
{
    float4 svPosition : SV_Position;
};

[shader("vertex")]
VertexOutput VertexMain(in uint vertexID : SV_VertexID)
{
    VertexOutput output;
    output.svPosition = float4(kPositions[vertexID], 0, 1);
    return output;
}

[shader("fragment")]
float4 FragmentMain(in VertexOutput input) : SV_Target
{
    uint2 outputCoord = uint2(input.svPosition.xy);
    uint2 inputCoord = outputCoord * 2u;
    uint2 maxInputCoord = PC.inputResolution - 1u;
    float4 color =
        SceneColor[min(inputCoord, maxInputCoord)]
        + SceneColor[min(inputCoord + uint2(1u, 0u), maxInputCoord)]
        + SceneColor[min(inputCoord + uint2(0u, 1u), maxInputCoord)]
        + SceneColor[min(inputCoord + uint2(1u, 1u), maxInputCoord)];
    return color * 0.25;
}
