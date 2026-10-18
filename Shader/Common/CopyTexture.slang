[[vk::binding(0, 0)]]
Texture2D<float4> SourceTexture;

struct VertexOutput
{
    float4 svPosition : SV_Position;
    float2 texCoord : TEXCOORD0;
};

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

static constexpr float2 kTexCoords[3] = {
    float2(0, 2),
    float2(2, 0),
    float2(0, 0)
};

[shader("vertex")]
VertexOutput VertexMain(in uint vertexID : SV_VertexID)
{
    VertexOutput output;
    output.svPosition = float4(kPositions[vertexID], 0, 1);
    output.texCoord = kTexCoords[vertexID];
    return output;
}

[shader("fragment")]
float4 FragmentMain(in VertexOutput input) : SV_Target
{
    uint width;
    uint height;
    SourceTexture.GetDimensions(width, height);

    uint2 coord = min(
        uint2(input.texCoord * float2(width, height)),
        uint2(width - 1, height - 1)
    );
    return SourceTexture[coord];
}
