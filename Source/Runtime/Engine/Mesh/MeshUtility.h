#pragma once
#include "StaticMesh.h"

#include <cstdint>
#include <vector>

namespace hyp
{
    struct MeshSDFBakeParams
    {
        // 每米体素数, 跟 UE r.DistanceFields.DefaultVoxelDensity 默认值(0.2/cm)一致
        float voxelDensity = 20.0f;
        // 跟 UE r.DistanceFields.MaxPerMeshResolution 默认值一致, 用法见 bakeMeshDistanceFieldCore()
        int32_t maxResolutionPerAxis = 256;
        int32_t narrowBandVoxels = 4;
    };

    struct MeshSDFBakeResult
    {
        int3 resolution{};
        float3 boundsMin{};
        float3 boundsMax{};
        float narrowBandDistance = 0.0f;
        std::vector<float> denseDistances; // 体素角点采样, (resolution+1) 三轴乘积个元素, 未做稀疏化压缩
    };

    struct MeshCardBakeParams
    {
        // 目标体素边长(米)——自适应循环收敛之后, 实际体素边长不会小于这个值(网格
        // 越大, 为了不超过 targetNumSurfels 预算, 会被迫用更大的边长, 但不会主动
        // 用比它更精细的边长)
        float targetVoxelSize = 0.1f;
        int32_t targetNumSurfels = 10000; // 六个方向 surfel 总数预算, 决定体素分辨率能有多细
        int32_t initialMaxVoxels = 64;    // 单轴体素数上限的起始值, 超预算就减半重来
        int32_t raysPerCell = 32;         // 每个体素格发射的采样光线数, 决定 coverage(命中比例)的量化精度
        float normalThreshold = 0.25f;    // 判定命中三角形是否算"正面"的 N·D 阈值

        int32_t hemisphereGridSize = 6;              // 半球可见性测试的分层网格边长, 总采样数=边长²
        float hemisphereHitRatioThreshold = 0.8f;     // 半球采样命中比例超过这个值, 才有资格被判定为埋在实体内部
        float hemisphereBackfaceRatioThreshold = 0.2f; // 命中里背面命中占比超过这个值, 才判定为埋在实体内部(命中比例和背面比例两个条件同时满足才剔除)

        float minClusterDensity = 0.2f / 3.0f; // 聚类合法性判据之一: coverage/覆盖面积算出的密度必须超过这个阈值, 排除稀疏、不成片的候选聚类
        float minClusterCoverage = 15.0f;      // 聚类合法性判据之一: 加权覆盖率总和(原始累加值, 不是归一化比例)必须达到这个阈值

        int32_t maxLumenMeshCards = 12; // 全局限量阶段保留的卡片数量上限
    };

    struct MeshCardBakeResult
    {
        Boxf meshCardsBounds;
        bool mostlyTwoSided = false;
        std::vector<MeshCard> cards;
    };

    // Mesh Card / MDF(Mesh Distance Field)烘焙能力的抽象接口——具体实现住在
    // Source/Editor/MeshUtility, 编辑期专属工具库(依赖 Embree), Engine 作为更底层的库不能
    // 反过来依赖它, 只能通过 ModuleServiceRegistry(见 Core/Common/ModuleServiceRegistry.h)
    // 按接口类型查找已注册的实现。运行期(未构建/未链接 Editor 工具链的配置)不存在任何实现,
    // getService<IMeshUtility>() 返回 nullptr。
    class IMeshUtility
    {
    public:
        virtual ~IMeshUtility() = default;

        // 固定用 mesh 的 LOD0 几何(全 LOD 共享的形状代理, 需要最高保真度), 实现内部按需建
        // BVH。返回稠密体素网格, 不做稀疏化压缩——调用方(StaticMesh::ensureDistanceFieldData())
        // 负责传给 setDistanceFieldData() 压缩存储+量化编码, 这里只管算出原始距离场
        virtual MeshSDFBakeResult bakeDistanceField(StaticMesh* mesh, const MeshSDFBakeParams& params) = 0;

        // 同样固定用 LOD0 几何, 实现内部按需建 BVH——跟 bakeDistanceField() 各自独立建,
        // 不共享(两者从不在同一调用时机被同时触发: Card 只在导入时烘一次, MDF 只在
        // 场景真正开启 Lumen 时才懒烘, 见 StaticMesh::bakeCardData()/ensureDistanceFieldData())
        virtual MeshCardBakeResult bakeCards(StaticMesh* mesh, const MeshCardBakeParams& params) = 0;
    };
}
