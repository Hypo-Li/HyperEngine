#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <Core/Math/Vector.h>

namespace hyp
{
    class Mesh;
}

namespace hyp::ed
{
    struct RayCastHit
    {
        float distance;
        hyp::float3 normal; // 未归一化的几何法线
        uint32_t triangleIndex; // 命中三角形在网格 LOD0 索引缓冲区里的三角形序号(第几个三元组)
    };

    struct NearestPointHit
    {
        float distance;
        hyp::float3 position;
        uint32_t triangleIndex;
    };

    // 网格三角形的光线求交加速结构——抽象接口, 不暴露具体用哪个库实现(当前实现在
    // MeshBVH.cpp 里用 Embree, 但这是实现细节, 调用方不应该依赖、也不需要知道), 换成
    // 别的光线求交库不需要碰任何调用这个接口的代码。
    class MeshBVH
    {
    public:
        virtual ~MeshBVH() = default;

        // 沿 direction(需要是单位向量)从 origin 出发求最近交点, maxDistance 是搜索上限
        virtual std::optional<RayCastHit> castRay(const hyp::float3& origin, const hyp::float3& direction, float maxDistance) const = 0;

        // 找 point 半径 maxRadius 内最近的表面点
        virtual std::optional<NearestPointHit> queryNearestPoint(const hyp::float3& point, float maxRadius) const = 0;
    };

    // 从 mesh 的第 lodLevel 级 LOD 几何构建 BVH。MDF 和 Mesh Card 烘焙都需要对同一份
    // 几何求交, 分别各自建一次是纯浪费(网格三角形数量多的时候这步不便宜)——需要同时
    // 烘两者时, 建一份 MeshBVH 传给两个函数共用即可:
    //
    //   std::unique_ptr<MeshBVH> bvh = createMeshBVH(mesh);
    //   bakeMeshDistanceField(mesh, *bvh);
    //   bakeMeshCards(mesh, *bvh);
    //
    // 固定用 LOD0: MDF/Mesh Card 都是全 LOD 共享的形状代理, 必须来自最高保真度几何——
    // 这个决定集中在这一处, 不需要每个烘焙函数各自重复声明"为什么固定用 LOD0"
    std::unique_ptr<MeshBVH> createMeshBVH(Mesh* mesh, uint32_t lodLevel = 0);

    struct MeshTwoSidedInfo
    {
        std::vector<bool> perTriangleTwoSided; // 按三角形序号索引(跟 RayCastHit::triangleIndex 对应)
        bool mostlyTwoSided = false;
    };

    // 跟 UE FEmbreeScene::bMostlyTwoSided 同一个判定: 双面材质三角形占比 >= 25%
    MeshTwoSidedInfo buildMeshTwoSidedInfo(Mesh* mesh, uint32_t lodLevel = 0);
}
