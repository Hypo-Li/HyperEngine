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

    // 核心算法, 不写回 mesh——返回稠密距离场(未做稀疏化压缩), 供 IMeshUtility 的具体实现
    // (MeshUtility::bakeDistanceField())直接复用, 不需要经过 StaticMesh::setDistanceFieldData()
    // 就能拿到结果。bvh/参数含义见 bakeMeshDistanceField() 的注释, 两者完全一致。
    MeshSDFBakeResult bakeMeshDistanceFieldCore(StaticMesh* mesh, const MeshBVH& bvh, const MeshSDFBakeParams& params = {});

    // 对 mesh 烘焙带符号距离场, 结果通过 mesh->setDistanceFieldData() 写回 mesh 本身。
    // bvh 必须是从同一个 mesh 的 LOD0 建出来的(用 createMeshBVH(mesh) 构造)——传进来
    // 而不是内部自己建, 是为了在同时烘 MDF 和 Mesh Card 时可以共用同一个 BVH, 不用对
    // 同一份几何重复建两次。
    //
    // 距离场是整个 Mesh 共享的一份形状代理(所有 LOD 渲染时用的是同一份, 见
    // Mesh::DistanceFieldData 的注释), 这也是为什么 bvh 必须来自 LOD0——用简化过的低模
    // 烘焙, 会让被简化掉的薄壁/细节直接决定这份"全 LOD 共享"代理的精度, 连渲染 LOD0
    // 本身都会因此失真, 跟"共享一份代理"的设计初衷矛盾。
    //
    // 分辨率和包围盒的算法照抄 UE GenerateSignedDistanceFieldVolumeData(): 每轴 brick 数 =
    // round(包围盒尺寸 * voxelDensity * DistanceFieldResolutionScale / kDistanceFieldUniqueBrickSize), 各轴独立
    // clamp, 分辨率是 brick 数的整数倍; 极端长条形网格在被 clamp 的轴上体素会比其它轴大。
    //
    // params.narrowBandVoxels: 窄带宽度(单位: 体素)——只有最近距离落在这个范围内的体素才
    //   需要做多方向光线投票判符号, 窄带外的体素直接按外部处理、距离统一记成窄带宽度本身,
    //   省掉大部分体素的投票开销, 也是 setDistanceFieldData() 能做稀疏化压缩+量化编码的前提
    //   (窄带外大片体素值相同, 可以整块跳过存储)。窄带半径按体素对角线长度算, 跟 UE 一致。
    void bakeMeshDistanceField(StaticMesh* mesh, const MeshBVH& bvh, const MeshSDFBakeParams& params = {});
}
