#include "MeshCard.h"

#include "MeshBVH.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <Core/Common/LogSystem.h>
#include <Core/Math/Box.h>
#include <Core/Math/Vector.h>
#include <Engine/Mesh/StaticMesh.h>

using hyp::Boxf;
using hyp::float3;
using hyp::int3;
using hyp::MeshCard;
using hyp::StaticMesh;
using hyp::ed::MeshBVH;
using hyp::ed::RayCastHit;

namespace
{

constexpr float kMeshCardsBoundsPadding = 0.01f;

float clampf(float value, float lo, float hi) noexcept
{
    return std::max(lo, std::min(value, hi));
}

// 32 位比特反转, 跟 (i+0.5)/N 搭配组成 Hammersley 低差异点集(一种确定性的均匀分布
// 采样序列), 比纯随机 jitter 更不容易在某些格子里扎堆、某些格子里空着
uint32_t reverseBits32(uint32_t x) noexcept
{
    x = ((x & 0x55555555u) << 1) | ((x >> 1) & 0x55555555u);
    x = ((x & 0x33333333u) << 2) | ((x >> 2) & 0x33333333u);
    x = ((x & 0x0F0F0F0Fu) << 4) | ((x >> 4) & 0x0F0F0F0Fu);
    x = ((x & 0x00FF00FFu) << 8) | ((x >> 8) & 0x00FF00FFu);
    x = (x << 16) | (x >> 16);
    return x;
}

bool isHitTwoSided(const RayCastHit& hit, const std::vector<bool>& perTriangleTwoSided) noexcept
{
    return hit.triangleIndex < perTriangleTwoSided.size() && perTriangleTwoSided[hit.triangleIndex];
}

// ---------------- 六个轴对齐方向 ----------------
// 跟 hyp::getMeshCardDirectionAxes() 的 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z 约定必须一致——
// mainIndex/sign 决定 AxisZ(法线, 该分量等于 sign), axis1Index/axis2Index 决定
// AxisX/AxisY(切线)分别是哪个分量, 两边各自独立定义但语义上是同一张表
struct DirectionConst
{
    int32_t mainIndex;
    int32_t sign;
    int32_t axis1Index;
    int32_t axis2Index;
};

constexpr DirectionConst kDirections[6] = {
    { 0, 1, 1, 2 },  // +X
    { 0, -1, 1, 2 }, // -X
    { 1, 1, 0, 2 },  // +Y
    { 1, -1, 0, 2 }, // -Y
    { 2, 1, 0, 1 },  // +Z
    { 2, -1, 0, 1 }, // -Z
};

// ---------------- Surfel / DirectionInfo / Cluster ----------------

struct SurfelSample
{
    float3 location;
    float3 normal;
};

struct Surfel
{
    int32_t cellZ = 0;
    int32_t minRayZ = 0;
    float coverage = 0.0f;
    float weightedCoverage = 0.0f;
    std::vector<SurfelSample> samples;
    int32_t cellX = 0;
    int32_t cellY = 0;
    bool discarded = false;
};

struct DirectionInfo
{
    int32_t mainIndex = 0;
    int32_t sign = 1;
    int32_t axis1Index = 0;
    int32_t axis2Index = 0;
    float3 voxelBoundsMin{};
    float voxelSize = 0.0f;
    float entryCoord = 0.0f;
    int32_t depthVoxels = 0;
};

struct Cluster
{
    int32_t nearPlane = 0;
    std::vector<uint32_t> indices; // 指向该方向自己的 surfels 数组
    int32_t minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    float coverage = 0.0f;
    float weightedCoverage = 0.0f;
};

// ---------------- 阶段 0: 包围盒 / 体素化参数 ----------------

// 网格自身包围盒往外扩一点 padding(每轴至少留 1cm), 避免网格表面正好卡在包围盒边缘
// 导致体素化时边界体素被截断
void computeMeshCardsBounds(const Boxf& meshBounds, float3& outBoundsMin, float3& outBoundsMax)
{
    const float3 center = meshBounds.getCenter();
    const float3 extent = meshBounds.getExtent();
    const float3 paddedExtent(
        std::max(extent.x + kMeshCardsBoundsPadding, kMeshCardsBoundsPadding),
        std::max(extent.y + kMeshCardsBoundsPadding, kMeshCardsBoundsPadding),
        std::max(extent.z + kMeshCardsBoundsPadding, kMeshCardsBoundsPadding));
    outBoundsMin = center - paddedExtent;
    outBoundsMax = center + paddedExtent;
}

void computeClusteringParams(
    const float3& meshCardsBoundsMin, const float3& meshCardsBoundsMax, float maxVoxels, float targetVoxelSize,
    float& outVoxelSize, int3& outSizeInVoxels, float3& outVoxelBoundsMin, float3& outVoxelBoundsMax, float& outAverageFaceArea)
{
    const float3 size = meshCardsBoundsMax - meshCardsBoundsMin;
    const float maxMeshCardsBounds = std::max({ size.x, size.y, size.z });

    const float maxSizeInVoxels = clampf(maxMeshCardsBounds / targetVoxelSize + 0.5f, 1.0f, maxVoxels);
    const float voxelSize = std::max(targetVoxelSize, maxMeshCardsBounds / maxSizeInVoxels);

    auto roundToVoxels = [&](float axisSize) -> int32_t
    {
        return static_cast<int32_t>(clampf(std::round(axisSize / voxelSize), 1.0f, maxVoxels));
    };

    const int3 sizeInVoxels(roundToVoxels(size.x), roundToVoxels(size.y), roundToVoxels(size.z));

    const float3 center = (meshCardsBoundsMin + meshCardsBoundsMax) * 0.5f;
    const float3 voxelBoundsExtent = float3(static_cast<float>(sizeInVoxels.x), static_cast<float>(sizeInVoxels.y), static_cast<float>(sizeInVoxels.z)) * voxelSize * 0.5f;

    outVoxelSize = voxelSize;
    outSizeInVoxels = sizeInVoxels;
    outVoxelBoundsMin = center - voxelBoundsExtent;
    outVoxelBoundsMax = center + voxelBoundsExtent;

    const int32_t sx = sizeInVoxels.x, sy = sizeInVoxels.y, sz = sizeInVoxels.z;
    outAverageFaceArea = 2.0f * static_cast<float>(sx * sy + sx * sz + sy * sz) / 6.0f;
}

// ---------------- 阶段 1: 六方向体素化 ----------------

struct MarchCandidate
{
    int32_t cellZ;
    int32_t minRayZ;
    float3 location;
    float3 normal;
};

// 沿 rayDir 连续穿透求交。origin 已经是回退了 nearPlaneOffset 之后的起点。
// - 命中三角形是双面处理、且 NdotD<0(几何法线朝向让它看起来像背面)时, 翻转 NdotD
//   和法线——双面材质没有真正的背面, 不能因为法线朝向被判成"看不见"。
// - HitCoordZ = clamp((累计距离-nearPlaneOffset)/voxelSize, 0, depthVoxels-1), 先
//   clamp 成浮点数再截断成整数(顺序不能反, 反了会在边界上差 1)。
// - 只有正面测试通过、且这次命中的层号严格大于"上次命中层号+1"才登记候选——保证同一根
//   光线连续穿透多层表面时, 每一层只登记一次, 不会因为在同一层内多次反弹式命中而重复。
// - 循环终止条件是"上次命中层号+1 < depthVoxels", 按已经穿透的层数判断, 不是按物理
//   距离判断——避免体素分辨率很低时因为浮点距离误差多走或少走一层。
std::vector<MarchCandidate> marchRayAndCollectCandidates(
    const MeshBVH& bvh, const float3& origin, const float3& rayDir, int32_t depthVoxels, float voxelSize,
    float nearPlaneOffset, const std::vector<bool>& perTriangleTwoSided, float normalThreshold)
{
    std::vector<MarchCandidate> candidates;
    int32_t lastLayer = -2; // 初值 -2 才能让 MinRayZ 哨兵和登记条件同时说得通
    float rayTNear = 0.0f;
    constexpr float kRayEpsilon = 1e-4f;

    while (lastLayer + 1 < depthVoxels)
    {
        const float3 currentOrigin = origin + rayDir * rayTNear;
        std::optional<RayCastHit> hit = bvh.castRay(currentOrigin, rayDir, std::numeric_limits<float>::infinity());
        if (!hit)
            break;

        const float hitT = rayTNear + hit->distance;
        // RayCastHit::normal 是 Embree 的几何法线 Ng, 长度正比于命中三角形面积(未归一化)
        // ——直接拿去跟 rayDir(单位向量)点乘, 结果的量级会被三角形面积稀释掉, 对着
        // normalThreshold 这种"看夹角余弦"的阈值比较之前必须先归一化, 否则 isFront
        // 基本永远判不成立(几乎所有真实网格的三角形面积都远小于 1)
        float3 normal = hit->normal;
        if (glm::length(normal) > 1e-8f)
            normal = glm::normalize(normal);
        float ndotd = glm::dot(-rayDir, normal);

        if (ndotd < 0.0f && isHitTwoSided(*hit, perTriangleTwoSided))
        {
            ndotd = -ndotd;
            normal = -normal;
        }

        const float rawCellZ = (hitT - nearPlaneOffset) / voxelSize;
        const int32_t cellZ = static_cast<int32_t>(clampf(rawCellZ, 0.0f, static_cast<float>(depthVoxels - 1)));

        const bool isFront = ndotd >= normalThreshold;
        if (isFront && cellZ >= 0 && cellZ > lastLayer + 1 && cellZ < depthVoxels)
        {
            const int32_t minRayZ = lastLayer >= 0 ? std::max(0, lastLayer + 1) : 0;
            candidates.push_back(MarchCandidate{ cellZ, minRayZ, currentOrigin + rayDir * hit->distance, normal });
        }

        lastLayer = cellZ;
        const float nextLayerT = nearPlaneOffset + static_cast<float>(lastLayer + 1) * voxelSize;
        rayTNear = std::max(nextLayerT, hitT) + kRayEpsilon;
    }

    return candidates;
}

struct VoxelizeResult
{
    std::vector<Surfel> surfels;
    DirectionInfo info;
};

// 对一个方向做体素化。体素是均匀正方体(三轴同一个 voxelSize), 不是按每个方向的
// footprint 各自独立算长宽比
VoxelizeResult voxelizeDirection(
    const MeshBVH& bvh, uint8_t directionIndex, const float3& voxelBoundsMin, const float3& voxelBoundsMax,
    float voxelSize, const int3& sizeInVoxels, const std::vector<bool>& perTriangleTwoSided, int32_t raysPerCell, float normalThreshold)
{
    const DirectionConst& dir = kDirections[directionIndex];
    const int32_t res1 = sizeInVoxels[dir.axis1Index];
    const int32_t res2 = sizeInVoxels[dir.axis2Index];
    const int32_t depthVoxels = sizeInVoxels[dir.mainIndex];

    const float nearPlaneOffset = 2.0f * voxelSize; // 光线起点回退量, 不是随手给的一点点 margin
    const float entryCoord = dir.sign > 0 ? voxelBoundsMax[dir.mainIndex] : voxelBoundsMin[dir.mainIndex];

    float3 rayDir(0.0f, 0.0f, 0.0f);
    rayDir[dir.mainIndex] = static_cast<float>(-dir.sign);

    std::vector<Surfel> surfels;

    for (int32_t i = 0; i < res1; ++i)
    {
        for (int32_t j = 0; j < res2; ++j)
        {
            std::vector<MarchCandidate> cellCandidates;

            for (int32_t sampleIndex = 0; sampleIndex < raysPerCell; ++sampleIndex)
            {
                const float jitterU = (static_cast<float>(sampleIndex) + 0.5f) / static_cast<float>(raysPerCell);
                const float jitterV = static_cast<float>(reverseBits32(static_cast<uint32_t>(sampleIndex))) / 4294967296.0f; // 2^32

                float3 origin(0.0f, 0.0f, 0.0f);
                origin[dir.mainIndex] = entryCoord + static_cast<float>(dir.sign) * nearPlaneOffset;
                origin[dir.axis1Index] = voxelBoundsMin[dir.axis1Index] + (static_cast<float>(i) + jitterU) * voxelSize;
                origin[dir.axis2Index] = voxelBoundsMin[dir.axis2Index] + (static_cast<float>(j) + jitterV) * voxelSize;

                std::vector<MarchCandidate> hits = marchRayAndCollectCandidates(
                    bvh, origin, rayDir, depthVoxels, voxelSize, nearPlaneOffset, perTriangleTwoSided, normalThreshold);

                for (MarchCandidate& hit : hits)
                    cellCandidates.push_back(hit);
            }

            // 按 (cellZ, minRayZ) 分组求平均——同一个体素格里, 不同方向jitter采样到同一层的
            // 命中合并成一个 surfel, coverage 就是命中比例
            struct Group
            {
                int32_t cellZ;
                int32_t minRayZ;
                std::vector<SurfelSample> samples;
            };
            std::vector<Group> groups;
            for (const MarchCandidate& c : cellCandidates)
            {
                auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g)
                    { return g.cellZ == c.cellZ && g.minRayZ == c.minRayZ; });
                if (it == groups.end())
                    groups.push_back(Group{ c.cellZ, c.minRayZ, { SurfelSample{ c.location, c.normal } } });
                else
                    it->samples.push_back(SurfelSample{ c.location, c.normal });
            }

            for (Group& g : groups)
            {
                Surfel s;
                s.cellZ = g.cellZ;
                s.minRayZ = g.minRayZ;
                s.coverage = static_cast<float>(g.samples.size()) / static_cast<float>(raysPerCell);
                s.samples = std::move(g.samples);
                s.cellX = i;
                s.cellY = j;
                surfels.push_back(std::move(s));
            }
        }
    }

    VoxelizeResult result;
    result.surfels = std::move(surfels);
    result.info.mainIndex = dir.mainIndex;
    result.info.sign = dir.sign;
    result.info.axis1Index = dir.axis1Index;
    result.info.axis2Index = dir.axis2Index;
    result.info.voxelBoundsMin = voxelBoundsMin;
    result.info.voxelSize = voxelSize;
    result.info.entryCoord = entryCoord;
    result.info.depthVoxels = depthVoxels;
    return result;
}

// ---------------- 阶段 2: 半球可见性测试 ----------------

std::vector<float3> generateHemisphereDirections(int32_t gridSize)
{
    std::vector<float3> directions;
    directions.reserve(static_cast<size_t>(gridSize) * gridSize);
    for (int32_t i = 0; i < gridSize; ++i)
    {
        for (int32_t j = 0; j < gridSize; ++j)
        {
            const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(gridSize);
            const float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(gridSize);
            const float z = u;
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float phi = 2.0f * 3.14159265358979323846f * v;
            directions.emplace_back(r * std::cos(phi), r * std::sin(phi), z);
        }
    }
    return directions;
}

void makeTangentBasis(const float3& normal, float3& outTangent, float3& outBitangent, float3& outNormal)
{
    const float3 n = glm::normalize(normal);
    const float3 reference = std::abs(n.z) < 0.9f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = glm::normalize(glm::cross(reference, n));
    const float3 bitangent = glm::cross(n, tangent);
    outTangent = tangent;
    outBitangent = bitangent;
    outNormal = n;
}

struct VisibilityResult
{
    float visibility;
    bool isBuried;
};

// 从这个 surfel 的命中点朝周围半球撒一圈光线, 统计命中比例和"命中的是背面"比例。双面
// 三角形没有真正的背面, 命中双面三角形不计入背面统计。返回的 visibility 是"逃逸比例"
// (没打中任何东西的光线占比), isBuried 是"是否应该判定为埋在实体内部、后续要丢弃"。
VisibilityResult computeVisibility(
    const MeshBVH& bvh, const std::vector<SurfelSample>& samples, float maxDistance,
    const std::vector<bool>& perTriangleTwoSided,
    const std::vector<float3>& hemisphereDirections, float hitRatioThreshold, float backfaceRatioThreshold)
{
    const int32_t rayCount = static_cast<int32_t>(hemisphereDirections.size());
    int32_t hitCount = 0;
    int32_t backfaceCount = 0;
    size_t sampleIndex = 0;

    for (const float3& localDir : hemisphereDirections)
    {
        // 逐根轮流拿一个真实命中点当起点, 不能改用这组命中点的平均位置——曲率大的地方
        // (耳朵边缘这类)平均点会落进表面内部, 让每根光线立刻打中自身背面, 把这个 surfel
        // 误判成 isBuried
        const SurfelSample& sample = samples[sampleIndex];
        sampleIndex = (sampleIndex + 1) % samples.size();

        float3 tangent, bitangent, n;
        makeTangentBasis(sample.normal, tangent, bitangent, n);
        const float3 origin = sample.location + sample.normal * 1e-3f; // 起点沿法线略微偏移, 避免射线起点正好卡在自己所在的三角形上发生自相交

        const float3 worldDir = tangent * localDir.x + bitangent * localDir.y + n * localDir.z;
        std::optional<RayCastHit> hit = bvh.castRay(origin, worldDir, maxDistance);
        if (!hit)
            continue;
        ++hitCount;

        if (glm::dot(hit->normal, worldDir) > 0.0f && !isHitTwoSided(*hit, perTriangleTwoSided))
            ++backfaceCount;
    }

    const int32_t escapeCount = rayCount - hitCount;
    const float visibility = static_cast<float>(escapeCount) / static_cast<float>(rayCount);
    const float hitRatio = static_cast<float>(hitCount) / static_cast<float>(rayCount);
    const float backfaceRatio = static_cast<float>(backfaceCount) / static_cast<float>(rayCount);
    const bool isBuried = hitRatio > hitRatioThreshold && backfaceRatio > backfaceRatioThreshold;

    return VisibilityResult{ visibility, isBuried };
}

// ---------------- 阶段 3: 按深度聚类 ----------------

// 把"从 nearPlane 这一层往表面外侧数"这个范围内、还没被别的聚类占用的 surfel 收集成
// 一个候选聚类。nearPlane 是这次聚类的起始深度层, cellZ 是每个 surfel 实际命中的深度层,
// minRayZ 是这条光线在命中这个 surfel 之前最后一次登记候选的深度层(见
// marchRayAndCollectCandidates)——一个 surfel 属于这个候选聚类当且仅当它自己的深度
// 不早于 nearPlane(cellZ>=nearPlane), 并且这条光线在到达 nearPlane 这一层之前没有
// 被更靠外的一层拦住(minRayZ<=nearPlane)。没有任何 surfel 满足条件就返回空。
std::optional<Cluster> buildCluster(int32_t nearPlane, const std::vector<Surfel>& surfels, const std::vector<bool>& assigned)
{
    Cluster cluster;
    cluster.nearPlane = nearPlane;
    bool any = false;

    for (uint32_t idx = 0; idx < surfels.size(); ++idx)
    {
        if (assigned[idx])
            continue;
        const Surfel& s = surfels[idx];
        if (s.cellZ >= nearPlane && s.minRayZ <= nearPlane)
        {
            if (!any)
            {
                cluster.minX = cluster.maxX = s.cellX;
                cluster.minY = cluster.maxY = s.cellY;
                cluster.minZ = cluster.maxZ = s.cellZ;
                any = true;
            }
            else
            {
                cluster.minX = std::min(cluster.minX, s.cellX);
                cluster.maxX = std::max(cluster.maxX, s.cellX);
                cluster.minY = std::min(cluster.minY, s.cellY);
                cluster.maxY = std::max(cluster.maxY, s.cellY);
                cluster.minZ = std::min(cluster.minZ, s.cellZ);
                cluster.maxZ = std::max(cluster.maxZ, s.cellZ);
            }
            cluster.indices.push_back(idx);
            cluster.coverage += s.coverage;
            cluster.weightedCoverage += s.weightedCoverage;
        }
    }

    if (!any)
        return std::nullopt;
    return cluster;
}

// 聚类覆盖的体素格数(用 I/J 方向上的实际跨度算, 不是聚类里的 surfel 个数——一个格子
// 可能因为多方向采样没有产生 surfel、也可能只是恰好没被这次聚类收进来)有多大比例被
// surfel 实际覆盖(用未加权的 coverage 累加值, 不是 weightedCoverage——密度只关心
// "这片区域填得实不实", 不需要可见性权重参与)
float computeClusterDensity(const Cluster& cluster)
{
    const float faceArea = static_cast<float>((cluster.maxX - cluster.minX + 1) * (cluster.maxY - cluster.minY + 1));
    return cluster.coverage / faceArea;
}

// 一个候选聚类要同时满足"够大"(加权覆盖率总和达到阈值——外层卡和内层卡的阈值不同,
// 外层卡的阈值还额外跟着平均面积走, 见调用方 minOuterCoverage 的计算)和"够密"(不是
// 稀疏的几个孤立点凑出来的一大片空区域)两个条件, 才会被提交成一张真正的卡
bool isClusterValid(const Cluster& cluster, float minOuterCoverage, float minDensity, float minClusterCoverage)
{
    const float minCoverage = cluster.nearPlane == 0 ? minOuterCoverage : minClusterCoverage;
    return cluster.weightedCoverage >= minCoverage && computeClusterDensity(cluster) > minDensity;
}

// 单个方向的完整聚类流程:
// 1. 先建 nearPlane=0(贴着表面最外层)的外层卡, 合法就提交, 这些 surfel 之后不再参与
//    聚类。
// 2. 如果这个方向的双面材质三角形占比不高(见 buildTwoSidedInfo), 说明网格在这个方向
//    上有明确的"实体内部"概念, 值得继续往深处剥层——反复做: 每一轮暴力扫描
//    nearPlane=1..depthVoxels-1 的每一个整数取值, 分别建一次候选聚类, 只保留其中
//    加权覆盖率最高的一个(如果合法)提交, 提交后这批 surfel 也从后续轮次里排除;
//    直到某一轮找不到任何合法候选为止。之所以每轮要重新扫描全部深度而不是直接从上一次
//    提交的深度往后取, 是因为不同深度层的 surfel 分布不均匀, 贪心地取"下一个未分配的
//    最浅深度"不一定能凑出一张合法的卡, 必须每轮都重新找全局最优的那一层。
std::vector<Cluster> buildSurfelClustersForDirection(
    const std::vector<Surfel>& surfels, int32_t depthVoxels, bool mostlyTwoSided,
    float minOuterCoverage, float minDensity, float minClusterCoverage)
{
    std::vector<bool> assigned(surfels.size(), false);
    for (size_t i = 0; i < surfels.size(); ++i)
        assigned[i] = surfels[i].discarded;

    std::vector<Cluster> clusters;

    std::optional<Cluster> outer = buildCluster(0, surfels, assigned);
    if (outer && isClusterValid(*outer, minOuterCoverage, minDensity, minClusterCoverage))
    {
        for (uint32_t idx : outer->indices)
            assigned[idx] = true;
        clusters.push_back(std::move(*outer));
    }

    if (!mostlyTwoSided)
    {
        while (true)
        {
            std::optional<Cluster> best;
            for (int32_t nearPlane = 1; nearPlane < depthVoxels; ++nearPlane)
            {
                std::optional<Cluster> candidate = buildCluster(nearPlane, surfels, assigned);
                if (!candidate)
                    continue;
                if (isClusterValid(*candidate, minOuterCoverage, minDensity, minClusterCoverage))
                {
                    if (!best || candidate->weightedCoverage > best->weightedCoverage)
                        best = std::move(candidate);
                }
            }

            if (!best)
                break;

            for (uint32_t idx : best->indices)
                assigned[idx] = true;
            clusters.push_back(std::move(*best));
        }
    }

    return clusters;
}

// ---------------- 阶段 4: 全局限量 ----------------

struct ClusterRef
{
    uint8_t directionIndex;
    const Cluster* cluster;
};

std::array<std::vector<Cluster>, 6> limitClustersGlobally(const std::array<std::vector<Cluster>, 6>& allClusters, int32_t maxCards)
{
    std::vector<ClusterRef> flattened;
    for (uint8_t d = 0; d < 6; ++d)
        for (const Cluster& c : allClusters[d])
            flattened.push_back(ClusterRef{ d, &c });

    std::sort(flattened.begin(), flattened.end(), [](const ClusterRef& a, const ClusterRef& b)
        { return a.cluster->weightedCoverage > b.cluster->weightedCoverage; });

    if (static_cast<int32_t>(flattened.size()) > maxCards)
        flattened.resize(maxCards);

    std::array<std::vector<Cluster>, 6> kept;
    for (const ClusterRef& ref : flattened)
        kept[ref.directionIndex].push_back(*ref.cluster);

    return kept;
}

// 把一个聚类(体素格坐标+深度范围)换算成实际的世界空间卡片中心/半宽:
// - footprint(横截面)两轴直接把聚类覆盖的整数体素格范围转成对应的世界坐标区间。
// - 深度轴留了不对称的余量: 近端(靠外)只留半个体素, 远端(靠内)留 1.5 个体素——
//   往内多留是因为聚类记录的 maxZ 只是"最后一个被登记为候选的深度层", 实际表面
//   可能还要再往内延伸一点点才闭合, 多留一点余量比卡得太死更安全。
// - 最后把结果夹回网格包围盒范围内: 横截面两轴不额外放宽, 深度轴额外放宽
//   kMarginZ——网格未来如果做位移贴图之类会让实际表面偏离原始三角形位置的效果,
//   深度方向留出这点余量可以让卡片依然覆盖到偏移后的表面, 不会被裁得太紧。
MeshCard computeCardOBB(const DirectionInfo& info, const Cluster& cluster, const float3& meshCardsBoundsMin, const float3& meshCardsBoundsMax, uint8_t directionIndex)
{
    constexpr float kMarginZ = 0.1f; // 米, 深度方向的额外余量

    float world1Min = info.voxelBoundsMin[info.axis1Index] + static_cast<float>(cluster.minX) * info.voxelSize;
    float world1Max = info.voxelBoundsMin[info.axis1Index] + static_cast<float>(cluster.maxX + 1) * info.voxelSize;
    float world2Min = info.voxelBoundsMin[info.axis2Index] + static_cast<float>(cluster.minY) * info.voxelSize;
    float world2Max = info.voxelBoundsMin[info.axis2Index] + static_cast<float>(cluster.maxY + 1) * info.voxelSize;

    const int32_t nearLayer = cluster.minZ;
    const int32_t farLayer = cluster.maxZ;
    const float nearDist = (static_cast<float>(nearLayer) - 0.5f) * info.voxelSize;
    const float farDist = (static_cast<float>(farLayer) + 1.5f) * info.voxelSize;

    // entryCoord 沿主轴, 往 -sign 方向(即 rayDir 方向)推进 dist 就是往网格内部走
    const float nearPosMain = info.entryCoord - static_cast<float>(info.sign) * nearDist;
    const float farPosMain = info.entryCoord - static_cast<float>(info.sign) * farDist;

    // 夹回 meshCardsBounds(padding过的原始网格包围盒), X/Y 不留余量, 深度方向留 MarginZ
    const float axis1Lo = std::min(meshCardsBoundsMin[info.axis1Index], meshCardsBoundsMax[info.axis1Index]);
    const float axis1Hi = std::max(meshCardsBoundsMin[info.axis1Index], meshCardsBoundsMax[info.axis1Index]);
    const float axis2Lo = std::min(meshCardsBoundsMin[info.axis2Index], meshCardsBoundsMax[info.axis2Index]);
    const float axis2Hi = std::max(meshCardsBoundsMin[info.axis2Index], meshCardsBoundsMax[info.axis2Index]);
    const float mainLo = std::min(meshCardsBoundsMin[info.mainIndex], meshCardsBoundsMax[info.mainIndex]) - kMarginZ;
    const float mainHi = std::max(meshCardsBoundsMin[info.mainIndex], meshCardsBoundsMax[info.mainIndex]) + kMarginZ;

    world1Min = std::max(world1Min, axis1Lo);
    world1Max = std::min(world1Max, axis1Hi);
    world2Min = std::max(world2Min, axis2Lo);
    world2Max = std::min(world2Max, axis2Hi);

    float mainLoPos = std::min(nearPosMain, farPosMain);
    float mainHiPos = std::max(nearPosMain, farPosMain);
    mainLoPos = std::max(mainLoPos, mainLo);
    mainHiPos = std::min(mainHiPos, mainHi);

    float3 center(0.0f, 0.0f, 0.0f);
    center[info.mainIndex] = (mainLoPos + mainHiPos) * 0.5f;
    center[info.axis1Index] = (world1Min + world1Max) * 0.5f;
    center[info.axis2Index] = (world2Min + world2Max) * 0.5f;

    float3 halfExtents(0.0f, 0.0f, 0.0f);
    halfExtents[info.mainIndex] = std::abs(mainHiPos - mainLoPos) * 0.5f;
    halfExtents[info.axis1Index] = (world1Max - world1Min) * 0.5f;
    halfExtents[info.axis2Index] = (world2Max - world2Min) * 0.5f;

    MeshCard card;
    card.center = center;
    card.halfExtents = halfExtents;
    card.directionIndex = directionIndex;
    return card;
}

} // namespace

namespace hyp::ed
{

MeshCardBakeResult bakeMeshCardsCore(StaticMesh* mesh, const MeshBVH& bvh, const MeshCardBakeParams& params)
{
    assert(mesh);

    // Mesh Card 是全 LOD 共享的形状代理, 固定用最高保真度的 LOD0——理由同 bakeMeshDistanceField
    constexpr uint32_t kSourceLODLevel = 0;

    float3 meshCardsBoundsMin, meshCardsBoundsMax;
    computeMeshCardsBounds(mesh->getOrUpdateBoundingBox(), meshCardsBoundsMin, meshCardsBoundsMax);

    const MeshTwoSidedInfo twoSidedInfo = buildMeshTwoSidedInfo(mesh, kSourceLODLevel);
    const bool mostlyTwoSided = twoSidedInfo.mostlyTwoSided;

    // ---- 体素预算自适应循环 ----
    float maxVoxels = static_cast<float>(params.initialMaxVoxels);
    float voxelSize = 0.0f;
    int3 sizeInVoxels{};
    float3 voxelBoundsMin{}, voxelBoundsMax{};
    float averageFaceArea = 0.0f;
    std::array<std::vector<Surfel>, 6> allSurfels;
    std::array<DirectionInfo, 6> directionInfos;

    while (true)
    {
        computeClusteringParams(meshCardsBoundsMin, meshCardsBoundsMax, maxVoxels, params.targetVoxelSize,
            voxelSize, sizeInVoxels, voxelBoundsMin, voxelBoundsMax, averageFaceArea);

        int32_t totalSurfels = 0;
        for (uint8_t d = 0; d < 6; ++d)
        {
            VoxelizeResult result = voxelizeDirection(
                bvh, d, voxelBoundsMin, voxelBoundsMax, voxelSize, sizeInVoxels,
                twoSidedInfo.perTriangleTwoSided, params.raysPerCell, params.normalThreshold);
            totalSurfels += static_cast<int32_t>(result.surfels.size());
            allSurfels[d] = std::move(result.surfels);
            directionInfos[d] = result.info;
        }

        if (totalSurfels <= params.targetNumSurfels || maxVoxels <= 1.0f)
            break;
        maxVoxels *= 0.5f;
    }

    const float minOuterCoverage = std::min(params.minClusterCoverage, 0.5f * averageFaceArea);
    const float minDensity = params.minClusterDensity;

    // ---- 阶段 2: 半球可见性测试 ----
    const std::vector<float3> hemisphereDirections = generateHemisphereDirections(params.hemisphereGridSize);
    const float diagonal = glm::length(meshCardsBoundsMax - meshCardsBoundsMin);
    const float maxRayDistance = diagonal * 2.0f;

    for (uint8_t d = 0; d < 6; ++d)
    {
        for (Surfel& s : allSurfels[d])
        {
            VisibilityResult visibility = computeVisibility(
                bvh, s.samples, maxRayDistance, twoSidedInfo.perTriangleTwoSided,
                hemisphereDirections, params.hemisphereHitRatioThreshold, params.hemisphereBackfaceRatioThreshold);
            s.weightedCoverage = s.coverage * (visibility.visibility + 1.0f);
            s.discarded = visibility.isBuried;
        }
    }

    // ---- 阶段 3: 聚类 ----
    std::array<std::vector<Cluster>, 6> allClusters;
    for (uint8_t d = 0; d < 6; ++d)
    {
        allClusters[d] = buildSurfelClustersForDirection(
            allSurfels[d], directionInfos[d].depthVoxels, mostlyTwoSided, minOuterCoverage, minDensity, params.minClusterCoverage);
    }

    // ---- 阶段 4: 全局限量 ----
    std::array<std::vector<Cluster>, 6> keptClusters = limitClustersGlobally(allClusters, params.maxLumenMeshCards);

    std::vector<MeshCard> cards;
    for (uint8_t d = 0; d < 6; ++d)
    {
        for (const Cluster& cluster : keptClusters[d])
            cards.push_back(computeCardOBB(directionInfos[d], cluster, meshCardsBoundsMin, meshCardsBoundsMax, d));
    }

    // mostlyTwoSided 会让 buildSurfelClustersForDirection 整个跳过按深度剥层建内层卡(见该
    // 函数里 if (!mostlyTwoSided) 那一段), 每个方向只剩最外层一张卡、总数退化到 6 张。对
    // 树叶/栅栏这类薄壳几何这是正确的(壳的另一侧不是"内部"), 但对实心网格就意味着所有自遮挡
    // 区域(耳朵内侧、脖子被头挡住的部分等)都没有任何卡片代表它们——采样阶段
    // (MeshSDFScene.slang 的深度可见性检查)会正确地拒绝这些点、退化成中性灰兜底。判定阈值
    // 是"双面三角形占比 >= 25%"(见 buildTwoSidedInfo), 实心网格如果材质误标成双面就会踩中。
    // 卡片总数和这个判定结果直接决定 surface cache 的覆盖率, 出现大面积兜底灰时先看这两个值
    LOG_INFO("bakeMeshCards: {} cards, mostlyTwoSided={} (双面占比 >=25% 会跳过深度剥层, 每方向只留最外层卡)",
        cards.size(), mostlyTwoSided);

    MeshCardBakeResult result;
    result.meshCardsBounds = Boxf(meshCardsBoundsMin, meshCardsBoundsMax);
    result.mostlyTwoSided = mostlyTwoSided;
    result.cards = std::move(cards);
    return result;
}

void bakeMeshCards(StaticMesh* mesh, const MeshBVH& bvh, const MeshCardBakeParams& params)
{
    MeshCardBakeResult result = bakeMeshCardsCore(mesh, bvh, params);
    mesh->setCardData(result.meshCardsBounds, result.mostlyTwoSided, std::move(result.cards));
}

} // namespace hyp::ed
