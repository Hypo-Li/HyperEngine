#include "MeshBVH.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

#include <embree4/rtcore.h>

#include <Core/Math/Vector.h>
#include <Engine/Material/MaterialInstance.h>
#include <Engine/Mesh/Mesh.h>

using hyp::double3;
using hyp::float3;

namespace
{

// MaterialInstance/MaterialTemplate 各自独立实现了双面性(前者可能覆盖模板默认值,
// 后者是模板自己的默认值), Mesh::getMaterial() 只能拿到基类 Material* ——按实际
// 子类型分别取, 都不是就按单面处理(没有材质、或者材质类型未来又扩展出新子类的情况)
bool isMaterialDoubleSided(hyp::Material* material)
{
    if (!material)
        return false;
    if (hyp::MaterialInstance::staticClass() == material->getClass())
        return static_cast<hyp::MaterialInstance*>(material)->getEffectiveDoubleSided();
    if (hyp::MaterialTemplate::staticClass() == material->getClass())
        return static_cast<hyp::MaterialTemplate*>(material)->isDoubleSided();
    return false;
}

// 点到三角形最近点(Ericson, Real-Time Collision Detection 5.1.5), 跟 UE ClosestPointOnTriangleToPoint 一样用 double 计算
double3 closestPointOnTriangle(const double3& p, const double3& a, const double3& b, const double3& c)
{
    const double3 ab = b - a;
    const double3 ac = c - a;
    const double3 ap = p - a;
    const double d1 = glm::dot(ab, ap);
    const double d2 = glm::dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0)
        return a;

    const double3 bp = p - b;
    const double d3 = glm::dot(ab, bp);
    const double d4 = glm::dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3)
        return b;

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
        return a + ab * (d1 / (d1 - d3));

    const double3 cp = p - c;
    const double d5 = glm::dot(ab, cp);
    const double d6 = glm::dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
        return c;

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
        return a + ac * (d2 / (d2 - d6));

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

    // 用平面投影而非重心重建, 使轴对齐面上最近点的 xy 与查询点逐位相同(同 UE PointPlaneProject)
    const double3 normal = glm::normalize(glm::cross(ab, ac));
    return p - normal * (glm::dot(p, normal) - glm::dot(a, normal));
}

struct PointQueryContext
{
    std::span<const float3> positions;
    std::span<const uint32_t> indices;
    float3 point{};
    unsigned primID = RTC_INVALID_GEOMETRY_ID;
};

bool closestPointCallback(RTCPointQueryFunctionArguments* args)
{
    auto* ctx = static_cast<PointQueryContext*>(args->userPtr);
    const unsigned primID = args->primID;

    const unsigned i0 = ctx->indices[static_cast<size_t>(primID) * 3 + 0];
    const unsigned i1 = ctx->indices[static_cast<size_t>(primID) * 3 + 1];
    const unsigned i2 = ctx->indices[static_cast<size_t>(primID) * 3 + 2];
    const float3& a = ctx->positions[i0];
    const float3& b = ctx->positions[i1];
    const float3& c = ctx->positions[i2];

    const float3 queryPoint(args->query->x, args->query->y, args->query->z);
    const float3 closest(closestPointOnTriangle(double3(queryPoint), double3(a), double3(b), double3(c)));
    const float dist = glm::length(queryPoint - closest);

    if (dist < args->query->radius)
    {
        args->query->radius = dist; // 收缩搜索半径, 驱动 BVH 剪枝
        ctx->point = closest;
        ctx->primID = primID;
        return true;
    }
    return false;
}

} // namespace

namespace hyp::ed
{

class EmbreeMeshBVH : public MeshBVH
{
public:
    EmbreeMeshBVH(Mesh* mesh, uint32_t lodLevel)
    {
        // rtcSetNewGeometryBuffer 按 RTC_FORMAT_FLOAT3 紧凑布局(3 个 float 一组)解读
        // 顶点数据, 要求 hyp::float3 本身没有额外的对齐填充, 否则直接 memcpy 会读出
        // 错位的数据
        static_assert(sizeof(float3) == 3 * sizeof(float), "hyp::float3 必须是紧凑的 3 float 布局才能直接喂给 Embree");

        mesh->accessLODData(lodLevel, [this, mesh, lodLevel](const Mesh::LODDataView& lod)
        {
            std::span<const float3> positions = lod.getPositions();
            std::span<const uint32_t> indices = lod.getIndices();

            // indices 是按 submesh(glTF primitive)各自局部编号的(0 起始, 相对这个 submesh
            // 自己的顶点区间), 顶点数据则是各 submesh 依次拼接进同一段连续缓冲区——两者要配
            // 起来必须给每个 submesh 的索引各自加上它自己的 vertexOffset, 换算成相对
            // positions 整个数组的绝对下标。渲染路径是靠 draw call 的 vertexOffset 参数
            // (GeometryProxy::Section::vertexOffset, 见 Mesh.cpp::updateGeometryProxy)
            // 让 GPU 做这个换算, 这里没有等价的机制, 必须显式做。只有一个 submesh 时
            // vertexOffset 恒为 0, 局部索引正好等于全局索引, 这个换算被巧合掩盖——这也是
            // 为什么这个 bug 只在有 2 个以上材质(submesh)的网格上才会表现出来(第二个及
            // 之后的 submesh 全部读到错误的顶点, BVH 里那部分三角形形状是乱的)
            std::vector<uint32_t> globalIndices(indices.begin(), indices.end());
            const uint32_t submeshCount = mesh->getSubmeshCount(lodLevel);
            for (uint32_t s = 0; s < submeshCount; ++s)
            {
                const int32_t vertexOffset = mesh->getSubmeshVertexOffset(lodLevel, s);
                if (vertexOffset == 0)
                    continue;

                const uint32_t indexOffset = mesh->getSubmeshIndexOffset(lodLevel, s);
                const uint32_t indexCount = mesh->getSubmeshIndexCount(lodLevel, s);
                const uint32_t indexEnd = std::min(indexOffset + indexCount, static_cast<uint32_t>(globalIndices.size()));
                for (uint32_t i = indexOffset; i < indexEnd; ++i)
                    globalIndices[i] = static_cast<uint32_t>(static_cast<int64_t>(globalIndices[i]) + vertexOffset);
            }

            // queryNearestPoint 的最近点计算需要原始三角形顶点数据(见 closestPointCallback),
            // Embree 内部只留自己 memcpy 的一份、不提供读回接口, 这里额外自己存一份
            mPositions.assign(positions.begin(), positions.end());
            mIndices = std::move(globalIndices);

            mDevice = rtcNewDevice(nullptr);
            mScene = rtcNewScene(mDevice);
            RTCGeometry geom = rtcNewGeometry(mDevice, RTC_GEOMETRY_TYPE_TRIANGLE);

            float* vertices = static_cast<float*>(rtcSetNewGeometryBuffer(
                geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
                3 * sizeof(float), positions.size()));
            std::memcpy(vertices, positions.data(), positions.size() * sizeof(float3));

            unsigned* dstIndices = static_cast<unsigned*>(rtcSetNewGeometryBuffer(
                geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
                3 * sizeof(unsigned), mIndices.size() / 3));
            std::memcpy(dstIndices, mIndices.data(), mIndices.size() * sizeof(uint32_t));

            rtcCommitGeometry(geom);
            rtcAttachGeometry(mScene, geom);
            rtcReleaseGeometry(geom); // scene 持有引用, 这里可以立即释放

            rtcCommitScene(mScene);
        });
    }

    ~EmbreeMeshBVH() override
    {
        if (mScene)
            rtcReleaseScene(mScene);
        if (mDevice)
            rtcReleaseDevice(mDevice);
    }

    std::optional<RayCastHit> castRay(const float3& origin, const float3& direction, float maxDistance) const override
    {
        RTCRayHit rayHit{};
        rayHit.ray.org_x = origin.x;
        rayHit.ray.org_y = origin.y;
        rayHit.ray.org_z = origin.z;
        rayHit.ray.dir_x = direction.x;
        rayHit.ray.dir_y = direction.y;
        rayHit.ray.dir_z = direction.z;
        rayHit.ray.tnear = 1e-4f;
        rayHit.ray.tfar = maxDistance;
        rayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;

        RTCIntersectArguments args;
        rtcInitIntersectArguments(&args);
        rtcIntersect1(mScene, &rayHit, &args);

        if (rayHit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
            return std::nullopt;

        return RayCastHit{ rayHit.ray.tfar, float3(rayHit.hit.Ng_x, rayHit.hit.Ng_y, rayHit.hit.Ng_z), rayHit.hit.primID };
    }

    std::optional<NearestPointHit> queryNearestPoint(const float3& point, float maxRadius) const override
    {
        RTCPointQuery query{};
        query.x = point.x;
        query.y = point.y;
        query.z = point.z;
        query.time = 0.0f;
        query.radius = maxRadius; // 用搜索半径上限本身当初始半径, BVH 剪枝力度更大

        RTCPointQueryContext context; // 每次查询独立初始化, 线程本地, 不需要跨线程共享
        rtcInitPointQueryContext(&context);

        PointQueryContext ctx;
        ctx.positions = mPositions;
        ctx.indices = mIndices;
        rtcPointQuery(mScene, &query, &context, &closestPointCallback, &ctx);

        if (ctx.primID == RTC_INVALID_GEOMETRY_ID)
            return std::nullopt;

        return NearestPointHit{ glm::length(point - ctx.point), ctx.point, ctx.primID };
    }

private:
    RTCDevice mDevice = nullptr;
    RTCScene mScene = nullptr;
    std::vector<float3> mPositions;
    std::vector<uint32_t> mIndices;
};

std::unique_ptr<MeshBVH> createMeshBVH(Mesh* mesh, uint32_t lodLevel)
{
    return std::make_unique<EmbreeMeshBVH>(mesh, lodLevel);
}

MeshTwoSidedInfo buildMeshTwoSidedInfo(Mesh* mesh, uint32_t lodLevel)
{
    const uint32_t triangleCount = mesh->getLODIndexCount(lodLevel) / 3;

    MeshTwoSidedInfo info;
    info.perTriangleTwoSided.assign(triangleCount, false);

    const uint32_t submeshCount = mesh->getSubmeshCount(lodLevel);
    for (uint32_t s = 0; s < submeshCount; ++s)
    {
        const uint32_t materialIndex = mesh->getSubmeshMaterialIndex(lodLevel, s);
        const bool twoSided = materialIndex < mesh->getMaterialCount()
            ? isMaterialDoubleSided(mesh->getMaterial(materialIndex))
            : false;

        const uint32_t indexOffset = mesh->getSubmeshIndexOffset(lodLevel, s);
        const uint32_t indexCount = mesh->getSubmeshIndexCount(lodLevel, s);
        const uint32_t triStart = indexOffset / 3;
        const uint32_t triCount = indexCount / 3;
        for (uint32_t t = triStart; t < triStart + triCount && t < triangleCount; ++t)
            info.perTriangleTwoSided[t] = twoSided;
    }

    const uint32_t twoSidedCount = static_cast<uint32_t>(std::count(info.perTriangleTwoSided.begin(), info.perTriangleTwoSided.end(), true));
    info.mostlyTwoSided = static_cast<uint64_t>(twoSidedCount) * 4 >= std::max<uint32_t>(1, triangleCount);
    return info;
}

} // namespace hyp::ed
