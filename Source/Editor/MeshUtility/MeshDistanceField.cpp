#include "MeshDistanceField.h"

#include "MeshBVH.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <execution>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

#include <Core/Math/Box.h>
#include <Core/Math/Vector.h>
#include <Engine/Mesh/StaticMesh.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>

using hyp::Boxf;
using hyp::float3;
using hyp::int3;
using hyp::kDistanceFieldUniqueBrickSize;

namespace
{

// 以下几个常量跟 UE MeshDistanceFieldUtilities.cpp / DistanceFieldAtlas.h 的同名量一致, 长度单位从厘米换算成米
constexpr float kMinMeshBoundsExtent = 0.01f;
constexpr float kCentralDifferencingExpandInVoxels = 0.25f;
constexpr int32_t kMeshDistanceFieldObjectBorder = 1;
constexpr int32_t kMaxIndirectionDimension = 1024;

// UE FRandomStream 的线性同余序列
class RandomStream
{
public:
    explicit RandomStream(uint32_t seed) noexcept : mSeed(seed) {}

    float getFraction() noexcept
    {
        mSeed = mSeed * 196314165u + 907633515u;
        return std::bit_cast<float>(0x3F800000u | (mSeed >> 9)) - 1.0f;
    }

private:
    uint32_t mSeed;
};

float3 uniformSampleHemisphere(float fraction1, float fraction2)
{
    const double x = static_cast<double>(fraction1) * 2.0 - 1.0;
    const double y = static_cast<double>(fraction2) * 2.0 - 1.0;
    if (x == 0.0 && y == 0.0)
        return float3(0.0f);

    constexpr float kPi = 3.14159265358979323846f;
    float r;
    float theta;
    if (std::abs(x) > std::abs(y))
    {
        r = static_cast<float>(x);
        theta = static_cast<float>(kPi / 4 * (y / x));
    }
    else
    {
        r = static_cast<float>(y);
        theta = static_cast<float>(kPi / 2 - kPi / 4 * (x / y));
    }

    const float u = r * std::cos(theta);
    const float v = r * std::sin(theta);
    const float r2 = r * r;
    return float3(u * std::sqrt(2.0f - r2), v * std::sqrt(2.0f - r2), 1.0f - r2);
}

std::vector<float3> generateStratifiedUniformHemisphereSamples(int sampleCount, RandomStream& randomStream)
{
    const int sampleCountPerDim = static_cast<int>(std::sqrt(static_cast<float>(sampleCount)));

    std::vector<float3> samples;
    samples.reserve(static_cast<size_t>(sampleCountPerDim) * sampleCountPerDim);

    for (int indexX = 0; indexX < sampleCountPerDim; ++indexX)
    {
        for (int indexY = 0; indexY < sampleCountPerDim; ++indexY)
        {
            const float u1 = randomStream.getFraction();
            const float u2 = randomStream.getFraction();

            const float fraction1 = (static_cast<float>(indexX) + u1) / static_cast<float>(sampleCountPerDim);
            const float fraction2 = (static_cast<float>(indexY) + u2) / static_cast<float>(sampleCountPerDim);

            samples.push_back(uniformSampleHemisphere(fraction1, fraction2));
        }
    }
    return samples;
}

// 采样点会正好落在网格包围盒的面上, 所以方向集合里不能有躺在坐标平面内的方向
std::vector<float3> generateStratifiedUniformSphereDirections(int hemisphereSampleCount)
{
    RandomStream randomStream(0);
    std::vector<float3> directions = generateStratifiedUniformHemisphereSamples(hemisphereSampleCount, randomStream);

    for (float3 sample : generateStratifiedUniformHemisphereSamples(hemisphereSampleCount, randomStream))
    {
        sample.z *= -1.0f;
        directions.push_back(sample);
    }
    return directions;
}

const std::vector<float3>& getSampleDirections()
{
    // 方向集合跟具体网格无关, 只需要生成一次; 函数局部 static 的初始化在 C++11 起是
    // 线程安全的(magic statics), 烘焙的多线程 worker 并发调第一次也没问题。
    static const std::vector<float3> directions = generateStratifiedUniformSphereDirections(49);
    return directions;
}

// 从 p 朝一组预先算好的方向发射线, 统计命中背面(法线和光线同向, 说明是从内部往外打穿出去,
// 光线出发点必然在这个三角形的内侧)的方向数, 超过全部方向数的阈值比例就判定为 p 在网格内部。
//
// 这个符号判定不依赖网格局部拓扑一致性, 用多数投票代替了按最近特征取伪法线的传统做法,
// 能正确处理非流形、自相交、以及多个互相嵌套的独立闭合壳(比如脸部和眼球是两个重叠的壳,
// 眼球表面附近的最近特征伪法线会指向脸部内侧, 判成"在脸部实体内部"是错的, 但多数投票
// 不受单个局部特征误导)。
bool isPointInsideByRayVote(const hyp::ed::MeshBVH& bvh, const float3& p)
{
    constexpr float kBackfaceRatioThreshold = 0.25f;

    const std::vector<float3>& directions = getSampleDirections();
    int backfaceHitCount = 0;

    for (const float3& dir : directions)
    {
        std::optional<hyp::ed::RayCastHit> hit = bvh.castRay(p, dir, std::numeric_limits<float>::infinity());
        if (!hit)
            continue;

        // 几何法线未归一化, 但判断朝向只看跟光线方向点积的正负号, 不需要先归一化。
        if (glm::dot(hit->normal, dir) > 0.0f)
            ++backfaceHitCount;
    }

    return static_cast<float>(backfaceHitCount) > kBackfaceRatioThreshold * static_cast<float>(directions.size());
}

// 返回带符号距离: 正数在网格外部, 负数在内部。距离大小用最近点查询直接得到, 符号用
// isPointInsideByRayVote 的多方向光线投票判断。
//
// narrowBandDistance 同时用作最近点查询的搜索半径上限和"要不要做光线投票"的判据: 只有
// 最近距离落在这个范围内才需要精确符号, 落在窄带外的点直接按"外部, 距离等于
// narrowBandDistance"处理, 省掉那 98 条投票光线的开销——这也是窄带外体素值全部相同、
// setDistanceFieldData() 能把这些体素整块跳过存储的前提。
//
// 可以从多个线程并发调用——BVH 只读查询本身是线程安全的。
float querySignedDistance(const hyp::ed::MeshBVH& bvh, const float3& p, float narrowBandDistance)
{
    std::optional<hyp::ed::NearestPointHit> nearest = bvh.queryNearestPoint(p, narrowBandDistance);
    if (!nearest)
        return narrowBandDistance; // 窄带内没有任何图元, 直接按外部处理, 不做光线投票

    const bool inside = isPointInsideByRayVote(bvh, p);
    return inside ? -nearest->distance : nearest->distance;
}

} // namespace

namespace hyp::ed
{

MeshSDFBakeResult bakeMeshDistanceFieldCore(StaticMesh* mesh, const MeshBVH& bvh, const MeshSDFBakeParams& params)
{
    assert(mesh);

    const Boxf& meshBounds = mesh->getOrUpdateBoundingBox(); // LOD0 的原始包围盒, 跟 bvh 用的是同一份几何

    const float distanceFieldResolutionScale = mesh->getDistanceFieldResolutionScale();
    const float numVoxelsPerUnit = params.voxelDensity * distanceFieldResolutionScale;
    const int32_t maxBricksPerAxis = std::min(
        static_cast<int32_t>(std::lround(static_cast<float>(distanceFieldResolutionScale <= 1.0f ? params.maxResolutionPerAxis / 2 : params.maxResolutionPerAxis) / kDistanceFieldUniqueBrickSize)),
        kMaxIndirectionDimension - 1);

    auto computeBrickDimensions = [&](const float3& size)
    {
        return int3(
            std::clamp(static_cast<int32_t>(std::lround(size.x * numVoxelsPerUnit / kDistanceFieldUniqueBrickSize)), 1, maxBricksPerAxis),
            std::clamp(static_cast<int32_t>(std::lround(size.y * numVoxelsPerUnit / kDistanceFieldUniqueBrickSize)), 1, maxBricksPerAxis),
            std::clamp(static_cast<int32_t>(std::lround(size.z * numVoxelsPerUnit / kDistanceFieldUniqueBrickSize)), 1, maxBricksPerAxis));
    };

    const float3 meshBoundsCenter = meshBounds.getCenter();
    const float3 meshBoundsExtent = glm::max(meshBounds.getExtent(), float3(kMinMeshBoundsExtent));
    float3 localSpaceMeshBoundsMin = meshBoundsCenter - meshBoundsExtent;
    float3 localSpaceMeshBoundsMax = meshBoundsCenter + meshBoundsExtent;

    // 以双面为主的网格在包围盒上贴着顶点, 中心差分梯度会在交点处为零, 所以先外扩一小段
    if (buildMeshTwoSidedInfo(mesh).mostlyTwoSided)
    {
        const float3 size = localSpaceMeshBoundsMax - localSpaceMeshBoundsMin;
        const int3 dims = computeBrickDimensions(size);
        const float3 texelSize = size / (float3(dims * kDistanceFieldUniqueBrickSize) - float3(2.0f * kCentralDifferencingExpandInVoxels));
        localSpaceMeshBoundsMin -= texelSize;
        localSpaceMeshBoundsMax += texelSize;
    }

    const int3 brickDimensions = computeBrickDimensions(localSpaceMeshBoundsMax - localSpaceMeshBoundsMin);
    const int3 resolution = brickDimensions * kDistanceFieldUniqueBrickSize;

    // 每侧留 kMeshDistanceFieldObjectBorder 个体素的边界, 供梯度重建时的插值使用
    const float3 borderTexelSize = (localSpaceMeshBoundsMax - localSpaceMeshBoundsMin) / (float3(resolution) - float3(2.0f * kMeshDistanceFieldObjectBorder));
    const float3 boundsMin = localSpaceMeshBoundsMin - borderTexelSize;
    const float3 boundsMax = localSpaceMeshBoundsMax + borderTexelSize;
    const float3 extent = boundsMax - boundsMin;

    const float3 voxelSizePerAxis = extent / float3(resolution.x, resolution.y, resolution.z);

    // 跟 UE MaxDistanceForEncoding 一致: 体素对角线长度 * BandSizeInVoxels
    const float narrowBandDistance = params.narrowBandVoxels * glm::length(voxelSizePerAxis);

    // 跟 UE 一样在体素角点采样, 每轴 resolution + 1 个点
    const int3 sampleCount = resolution + 1;
    std::vector<float> denseDistances(static_cast<size_t>(sampleCount.x) * sampleCount.y * sampleCount.z);

    // 按 z 切片分给并行算法调度, 每个切片写自己独占的一段连续内存(不同切片的 z 范围
    // 不重叠), 没有跨线程依赖也没有 false sharing, 每个体素的最近点查询本来就互相独立,
    // 是天然的 embarrassingly parallel 场景。BVH 只读查询可以多线程并发, 不需要加锁。
    // 用 std::execution::par 而不是自己起 std::thread: 复用进程级线程池, 避免每次烘焙
    // 都重新创建/销毁一批 OS 线程; 调度器按运行时负载动态分派切片, 也比按 z 数量静态均分
    // 更能吸收窄带区域在 z 方向分布不均导致的单切片耗时差异(窄带内的体素要多做 98 条
    // 光线投票, 窄带外的体素只做一次最近点查询就返回, 单个体素开销差异很大)。
    std::vector<int> zIndices(static_cast<size_t>(sampleCount.z));
    std::iota(zIndices.begin(), zIndices.end(), 0);

    std::for_each(std::execution::par, zIndices.begin(), zIndices.end(), [&](int z)
    {
        for (int y = 0; y < sampleCount.y; ++y)
        {
            for (int x = 0; x < sampleCount.x; ++x)
            {
                const float3 p = boundsMin + float3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * voxelSizePerAxis;
                const size_t idx = (static_cast<size_t>(z) * sampleCount.y + y) * static_cast<size_t>(sampleCount.x) + x;
                denseDistances[idx] = querySignedDistance(bvh, p, narrowBandDistance);
            }
        }
    });

    MeshSDFBakeResult result;
    result.resolution = resolution;
    result.boundsMin = boundsMin;
    result.boundsMax = boundsMax;
    result.narrowBandDistance = narrowBandDistance;
    result.denseDistances = std::move(denseDistances);
    return result;
}

void bakeMeshDistanceField(StaticMesh* mesh, const MeshBVH& bvh, const MeshSDFBakeParams& params)
{
    MeshSDFBakeResult result = bakeMeshDistanceFieldCore(mesh, bvh, params);
    mesh->setDistanceFieldData(result.resolution, result.boundsMin, result.boundsMax, result.narrowBandDistance, std::move(result.denseDistances));
}

} // namespace hyp::ed
