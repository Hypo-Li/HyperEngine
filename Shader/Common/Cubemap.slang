#pragma once

float3 faceUVToCubemapDir(uint face, float2 uv)
{
    float3 dir;
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;
    switch (face)
    {
        case 0: dir = normalize(float3( 1.0,   -v,   -u)); break; // +X
        case 1: dir = normalize(float3(-1.0,   -v,    u)); break; // -X
        case 2: dir = normalize(float3(  u,    1.0,   v)); break; // +Y
        case 3: dir = normalize(float3(  u,   -1.0,  -v)); break; // -Y
        case 4: dir = normalize(float3(  u,   -v,   1.0)); break; // +Z
        case 5: dir = normalize(float3( -u,   -v,  -1.0)); break; // -Z
    }
    return dir;
}