#pragma once
#include "../Common/SparseDistanceField.slang"

// 跟 hyp::kGlobalDistanceFieldObjectGrid*(GlobalDistanceField.h)必须保持一致
static const uint kGlobalDistanceFieldObjectGridInvalidEntry = 0xFFFFFFFFu;
static const uint kGlobalDistanceFieldObjectGridCellCapacity = 4;
static const uint kGlobalDistanceFieldObjectGridCellsPerBrickAxis = 4;
static const uint kGlobalDistanceFieldObjectGridWordsPerBrick =
    kGlobalDistanceFieldObjectGridCellsPerBrickAxis * kGlobalDistanceFieldObjectGridCellsPerBrickAxis
    * kGlobalDistanceFieldObjectGridCellsPerBrickAxis * kGlobalDistanceFieldObjectGridCellCapacity;
static const float kGlobalDistanceFieldObjectGridCardInterpolationRangeInVoxels = 3.0;

uint globalDistanceFieldObjectGridCellLinearIndex(uint3 cellCoordInBrick)
{
    return (cellCoordInBrick.z * kGlobalDistanceFieldObjectGridCellsPerBrickAxis + cellCoordInBrick.y)
        * kGlobalDistanceFieldObjectGridCellsPerBrickAxis + cellCoordInBrick.x;
}

uint globalDistanceFieldObjectGridEntryOffset(uint brickSlot, uint cellLinearIndex)
{
    return brickSlot * kGlobalDistanceFieldObjectGridWordsPerBrick + cellLinearIndex * kGlobalDistanceFieldObjectGridCellCapacity;
}

// voxelExtent 是半个体素边长(globalDistanceFieldVoxelExtent())
float globalDistanceFieldObjectGridCellHalfExtent(float voxelExtent)
{
    return voxelExtent * float(kSparseUniqueBrickSize) / float(kGlobalDistanceFieldObjectGridCellsPerBrickAxis);
}

float globalDistanceFieldObjectGridMaxQueryDistance(float voxelExtent)
{
    return 1.44 * globalDistanceFieldObjectGridCellHalfExtent(voxelExtent)
        + kGlobalDistanceFieldObjectGridCardInterpolationRangeInVoxels * voxelExtent;
}

// 高 8 位量化距离、低 24 位 meshInstanceIndex, 按 uint 比较大小即按距离比较
uint packGlobalDistanceFieldObjectGridEntry(uint meshInstanceIndex, float distance, float voxelExtent)
{
    float cellHalfExtent = globalDistanceFieldObjectGridCellHalfExtent(voxelExtent);
    float maxQueryDistance = globalDistanceFieldObjectGridMaxQueryDistance(voxelExtent);
    float normalizedDistance = saturate((distance + 0.25 * cellHalfExtent) / (maxQueryDistance + 0.25 * cellHalfExtent));
    uint packedDistance = uint(clamp(normalizedDistance * 256.0, 0.0, 255.0));
    return (meshInstanceIndex & 0xFFFFFFu) | (packedDistance << 24);
}

uint unpackGlobalDistanceFieldObjectGridMeshInstanceIndex(uint entry)
{
    return entry & 0xFFFFFFu;
}

struct GlobalDistanceFieldObjectGridCell
{
    uint entries[kGlobalDistanceFieldObjectGridCellCapacity];
};

GlobalDistanceFieldObjectGridCell initGlobalDistanceFieldObjectGridCell()
{
    GlobalDistanceFieldObjectGridCell cell;
    for (uint i = 0; i < kGlobalDistanceFieldObjectGridCellCapacity; ++i)
        cell.entries[i] = kGlobalDistanceFieldObjectGridInvalidEntry;
    return cell;
}

void addToGlobalDistanceFieldObjectGridCell(inout GlobalDistanceFieldObjectGridCell cell, uint meshInstanceIndex, float distance, float voxelExtent)
{
    if (abs(distance) >= globalDistanceFieldObjectGridMaxQueryDistance(voxelExtent))
        return;

    uint entryToInsert = packGlobalDistanceFieldObjectGridEntry(meshInstanceIndex, distance, voxelExtent);

    uint maxEntry = 0;
    for (uint i = 0; i < kGlobalDistanceFieldObjectGridCellCapacity; ++i)
        maxEntry = max(maxEntry, cell.entries[i]);

    if (entryToInsert < maxEntry)
    {
        for (uint i = 0; i < kGlobalDistanceFieldObjectGridCellCapacity; ++i)
        {
            if (cell.entries[i] == maxEntry)
            {
                cell.entries[i] = entryToInsert;
                break;
            }
        }
    }
}

void sortGlobalDistanceFieldObjectGridCell(inout GlobalDistanceFieldObjectGridCell cell)
{
    for (uint pass = 0; pass + 1 < kGlobalDistanceFieldObjectGridCellCapacity; ++pass)
    {
        for (uint i = 0; i + pass + 1 < kGlobalDistanceFieldObjectGridCellCapacity; ++i)
        {
            uint a = min(cell.entries[i], cell.entries[i + 1]);
            uint b = max(cell.entries[i], cell.entries[i + 1]);
            cell.entries[i] = a;
            cell.entries[i + 1] = b;
        }
    }
}
