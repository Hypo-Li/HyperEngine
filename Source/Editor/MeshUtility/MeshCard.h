#pragma once
#include <cstdint>

#include <Engine/Mesh/MeshUtility.h>

namespace hyp
{
    class StaticMesh;
}

namespace hyp::ed
{
    class MeshBVH;

    // 核心算法, 不写回 mesh——返回卡片数据, 供 IMeshUtility 的具体实现
    // (MeshUtility::bakeCards())直接复用, 不需要经过 StaticMesh::setCardData() 就能拿到结果。
    // bvh/参数含义见 bakeMeshCards() 的注释, 两者完全一致。
    MeshCardBakeResult bakeMeshCardsCore(StaticMesh* mesh, const MeshBVH& bvh, const MeshCardBakeParams& params = {});

    // 对 mesh 生成朝六个轴对齐方向(+X/-X/+Y/-Y/+Z/-Z)投影的一批 Mesh Card, 结果通过
    // mesh->setCardData() 写回 mesh 本身。整体流程分四步:
    //
    // 1. 六方向体素化——对每个方向, 按体素网格逐格朝网格发射一批光线(带 Hammersley
    //    低差异 jitter), 记录每个体素格命中表面的位置/法线/覆盖率(surfel)。体素
    //    分辨率按 surfel 总数预算自适应: 从粗到细逐轮加密, 一旦六个方向 surfel 总数
    //    超过 targetNumSurfels 就退回上一轮, 不是固定分辨率。
    // 2. 半球可见性测试——对每个 surfel, 从它的表面位置朝周围半球撒一圈光线: 如果
    //    大部分光线都被命中且以背面命中为主, 说明这个 surfel 被埋在实体内部, 实际
    //    不可见, 标记为丢弃, 不参与后续聚类。
    // 3. 按深度聚类——同一方向内, 先取贴着表面最外层的一批 surfel 组成一张卡(外层
    //    卡); 如果这个方向的双面材质三角形占比不高(不是植被一类的薄壳), 再反复往
    //    更深处剥出新的一层(每轮暴力扫描所有可能的深度分层, 只留权重最高的一个提交),
    //    直到找不到合法的新一层为止——这一步用来捕捉同一方向上被遮挡的第二层表面。
    // 4. 全局限量——把六个方向所有卡片汇总, 按加权覆盖率降序排序, 只保留前
    //    maxLumenMeshCards 张。
    //
    // bvh 必须是从同一个 mesh 的 LOD0 建出来的(用 createMeshBVH(mesh) 构造)——传进来
    // 而不是内部自己建, 调用方按自己的场景决定是否要跟别的求交操作共用同一个 BVH。
    //
    // 双面性按 LOD0 每个 submesh 实际绑定的材质(MaterialInstance::getEffectiveDoubleSided()
    // / MaterialTemplate::isDoubleSided())逐三角形统计, 双面三角形占比是否 >=25% 的
    // 真实结果, 不需要调用方额外指定。
    void bakeMeshCards(StaticMesh* mesh, const MeshBVH& bvh, const MeshCardBakeParams& params = {});
}
