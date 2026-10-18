// 对应 UE 的 ScreenProbeRadianceSHAmbient/ScreenProbeRadianceSHDirectional 存储布局
// (LumenScreenProbeFiltering.usf:746-791、LumenScreenProbeGather.usf:800-830)
#pragma once
#include "../Common/SHCommon.slang"

float3 screenProbeRadianceSHAmbient(ThreeBandSHRGB sh)
{
    return float3(sh.r.v0.x, sh.g.v0.x, sh.b.v0.x);
}

// L0 存在 ambient 里, 剩下 8 个系数按 RGB 三通道各两个 float4 拆开
void screenProbeRadianceSHDirectional(ThreeBandSHRGB sh, uint channel, out float4 sh0, out float4 sh1)
{
    ThreeBandSH c = channel == 0u ? sh.r : (channel == 1u ? sh.g : sh.b);
    sh0 = float4(c.v0.yzw, c.v1.x);
    sh1 = float4(c.v1.yzw, c.v2);
}

// 沿 x 方向按整张图集的宽度跨步平铺, 不是挨着探针排
uint2 screenProbeRadianceSHDirectionalCoord(uint2 atlasCoord, uint atlasWidth, uint index)
{
    return uint2(atlasCoord.x + index * atlasWidth, atlasCoord.y);
}

ThreeBandSHRGB readScreenProbeRadianceSH(
    Texture2D<float4> ambientTexture,
    Texture2D<float4> directionalTexture,
    uint2 atlasCoord,
    uint atlasWidth)
{
    float3 ambient = ambientTexture[atlasCoord].rgb;

    float4 sh0Red = directionalTexture[screenProbeRadianceSHDirectionalCoord(atlasCoord, atlasWidth, 0u)];
    float4 sh1Red = directionalTexture[screenProbeRadianceSHDirectionalCoord(atlasCoord, atlasWidth, 1u)];
    float4 sh0Green = directionalTexture[screenProbeRadianceSHDirectionalCoord(atlasCoord, atlasWidth, 2u)];
    float4 sh1Green = directionalTexture[screenProbeRadianceSHDirectionalCoord(atlasCoord, atlasWidth, 3u)];
    float4 sh0Blue = directionalTexture[screenProbeRadianceSHDirectionalCoord(atlasCoord, atlasWidth, 4u)];
    float4 sh1Blue = directionalTexture[screenProbeRadianceSHDirectionalCoord(atlasCoord, atlasWidth, 5u)];

    ThreeBandSHRGB sh;
    sh.r.v0 = float4(ambient.x, sh0Red.xyz);
    sh.r.v1 = float4(sh0Red.w, sh1Red.xyz);
    sh.r.v2 = sh1Red.w;
    sh.g.v0 = float4(ambient.y, sh0Green.xyz);
    sh.g.v1 = float4(sh0Green.w, sh1Green.xyz);
    sh.g.v2 = sh1Green.w;
    sh.b.v0 = float4(ambient.z, sh0Blue.xyz);
    sh.b.v1 = float4(sh0Blue.w, sh1Blue.xyz);
    sh.b.v2 = sh1Blue.w;
    return sh;
}

float3 evaluateScreenProbeSHIrradiance(ThreeBandSHRGB radianceSH, float3 direction)
{
    return max(float3(0.0), 4.0 * PI * dotSH3(radianceSH, calcDiffuseTransferSH3(direction)));
}
