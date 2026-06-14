#pragma once

static constexpr float WORLD_TILE_SIZE = 1024.0f;

struct WorldCoord
{
    int3 tile;
    float3 offset;
};

struct WorldMatrix
{
    float3x4 RSOffset;
    int3 tile;

    WorldCoord getTranslation()
    {
        float3 offset = float3(RSOffset[0].w, RSOffset[1].w, RSOffset[2].w);
        return WorldCoord(this.tile, offset);
    }
};

void calculateLocalToView(
    in WorldMatrix localToWorld,
    in float3x3 worldToViewRot,
    in WorldCoord cameraOrigin,
    out float4x4 localToView
)
{
    float3 localOffset = float3(localToWorld.RSOffset[0].w, localToWorld.RSOffset[1].w, localToWorld.RSOffset[2].w);
    float3 relativePos = float3(localToWorld.tile - cameraOrigin.tile) * WORLD_TILE_SIZE + (localOffset - cameraOrigin.offset);
    float3x3 RS = mul(float3x3(localToWorld.RSOffset), worldToViewRot);
    float3 T = mul(relativePos, worldToViewRot);
    localToView = float4x4(
        float4(RS[0], 0),
        float4(RS[1], 0),
        float4(RS[2], 0),
        float4(T, 1)
    );
}
