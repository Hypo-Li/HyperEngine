#pragma once

typedef float2 MatUV;
typedef Texture2D MatTexture2D;
typedef Texture2DArray MatTexture2DArray;
typedef Texture3D MatTexture3D;
typedef TextureCube MatTextureCube;

namespace Material
{
    float4 sampleTexture2DArray(MatTexture2DArray atlas, SamplerState atlasSampler, MatUV uv, float page)
    {
        return atlas.Sample(atlasSampler, float3(uv, page));
    }
}
