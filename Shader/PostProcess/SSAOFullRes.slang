#define USE_AO_SETUP_AS_INPUT 0
#define USE_UPSAMPLE 1
#include "SSAOCommon.slang"

[shader("vertex")]
float4 VertexMain(in uint vertexID : SV_VertexID) : SV_Position
{
    return vertexMainImpl(vertexID);
}

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    return fragmentMainImpl(svPosition);
}
