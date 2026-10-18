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

// coord 相对 origin 的偏移, 以 origin 所在 tile 为参照系——tile 部分保持整数相减(精确),
// 只有落进同一个 tile 附近的小偏移才转 float, 这是 WorldCoord 全部大世界精度技巧的核心
float3 calculateRelativeWorldPosition(in WorldCoord coord, in WorldCoord origin)
{
    return float3(coord.tile - origin.tile) * WORLD_TILE_SIZE + (coord.offset - origin.offset);
}

// 平移要和 positionRWS 同一个相机相对参照系, 不能直接用 WorldMatrix.RSOffset 里基于 tile 的偏移
float3x4 calculateRelativeWorldToObject(in float3x3 worldToObjectRS, in WorldCoord objectOrigin, in WorldCoord cameraOrigin)
{
    const float3 translation = -mul(calculateRelativeWorldPosition(objectOrigin, cameraOrigin), worldToObjectRS);
    return float3x4(
        float4(worldToObjectRS[0], translation.x),
        float4(worldToObjectRS[1], translation.y),
        float4(worldToObjectRS[2], translation.z)
    );
}

// tileValue(整数, 可正可负) 乘上一个已经落在 [0, modulus) 内的小数, 再对 modulus 取模——
// "倍增取模"逐位展开 tileValue, 每一步都把中间值压回 [0, modulus) 内, 全程不出现数量级
// 很大的中间值, 不管 tileValue 多大都不损失精度(标准的模乘法技巧, 跟密码学"模幂"是同一类
// 思路)。用来在 GPU 上精确算"绝对世界坐标 mod 一个远小于 WORLD_TILE_SIZE 的数"这件事——
// 直接把 tile 转成 float 再乘再取模, 在 tile 很大时会因为 float 精度不够把结果错出好几个
// 周期, 这个函数就是为了避开这一步
float worldTileMulMod(int tileValue, float smallFactor, float modulus)
{
    uint n = uint(abs(tileValue));
    float b = smallFactor;
    float result = 0.0;
    for (uint i = 0; i < 32u; ++i)
    {
        if ((n & 1u) != 0u)
        {
            result += b;
            if (result >= modulus)
                result -= modulus;
        }
        b *= 2.0;
        if (b >= modulus)
            b -= modulus;
        n >>= 1u;
    }
    return (tileValue < 0) ? -result : result;
}

// 精确算出"这个世界坐标分量离最近的、边长 cellSize 的网格线有多远"(带符号, 范围
// [-cellSize/2, cellSize/2))——靠 worldTileMulMod() 避开大数乘法/取模的精度损失, 不管
// tileValue 多大都精确。先用取模的乘法分配律((a*b) mod m == (a*(b mod m)) mod m)把
// WORLD_TILE_SIZE 按 cellSize 取模成一个小数, 再用 worldTileMulMod() 算 tileValue 乘上
// 这个小数的取模结果, 最后跟 offsetValue 合并、转成以最近格线为参照的带符号余数
float worldCoordGridSnapDelta(int tileValue, float offsetValue, float cellSize)
{
    float base = fmod(WORLD_TILE_SIZE, cellSize);
    if (base < 0.0)
        base += cellSize;

    float tileMod = worldTileMulMod(tileValue, base, cellSize);
    float totalMod = fmod(tileMod + offsetValue, cellSize);
    if (totalMod < 0.0)
        totalMod += cellSize;

    return (totalMod < cellSize * 0.5) ? totalMod : (totalMod - cellSize);
}

// 上面那个函数的向量版本, 三个分量各自独立算(cellSize 三个轴共用同一个值)
float3 worldCoordGridSnapDelta(WorldCoord coord, float cellSize)
{
    return float3(
        worldCoordGridSnapDelta(coord.tile.x, coord.offset.x, cellSize),
        worldCoordGridSnapDelta(coord.tile.y, coord.offset.y, cellSize),
        worldCoordGridSnapDelta(coord.tile.z, coord.offset.z, cellSize)
    );
}

void calculateObjectToView(
    in WorldMatrix objectToWorld,
    in float3x3 worldToViewRot,
    in WorldCoord cameraOrigin,
    out float4x4 objectToView
)
{
    float3 relativePos = calculateRelativeWorldPosition(objectToWorld.getTranslation(), cameraOrigin);
    float3x3 RS = mul(float3x3(objectToWorld.RSOffset), worldToViewRot);
    float3 T = mul(relativePos, worldToViewRot);
    objectToView = float4x4(
        float4(RS[0], 0),
        float4(RS[1], 0),
        float4(RS[2], 0),
        float4(T, 1)
    );
}
