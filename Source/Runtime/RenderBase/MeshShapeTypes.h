#pragma once
#include <Core/Math/Vector.h>
#include <Core/Reflection/Reflection.h>

namespace hyp
{
    // 单张 Mesh Card 的形状定义, 局部空间。center 是卡片中心(不是角点), halfExtents 是
    // 沿卡片自己三个轴(见 getMeshCardDirectionAxes)的半宽。这份 struct 同时是资产序列化
    // 格式(Engine::Mesh::CardData::cards, 反射注册见 MeshShapeTypes.cpp)
    // 和 GPU 打包格式(MeshShapeProxy 构造函数的直接输入)——CPU 资产层和 GPU 消费层用同一份
    // 数据, 不需要在两层各自维护一份再互相转换。
    //
    // directionIndex 用 uint32_t 而不是更省空间的 uint8_t: MeshShapeProxy 把这个 struct
    // 手动扁平打包成 StructuredBuffer<uint>(7 个 uint32 一张卡, 见 MeshShapeProxy.cpp 的
    // static_assert), 要求整个 struct 紧凑排列成整数个 uint32, uint8_t 会导致跟前后的
    // float3 字段对不齐。
    struct MeshCard
    {
        float3 center{};
        float3 halfExtents{};
        uint32_t directionIndex = 0;
    };
    template <> Type* Reflection::createType<MeshCard>();

    // Mesh Card 的六个轴对齐方向对应的局部空间正交基——0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z,
    // outAxisZ 是这个方向的朝外法线, outAxisX/outAxisY 是它的切线。这是生成阶段
    // (MeshUtility 烘焙 Mesh Card 时, Engine 依赖 RenderBase 合法)和消费阶段(运行时
    // 按 directionIndex 还原卡片朝向, 消费方分布在 Engine 和 Renderer 两层, 只有两边都
    // 依赖的 RenderBase 放得下)共用的唯一约定, 只在这一处定义——不会为每张卡片单独存三个
    // 轴向量(重复存储, 而且同一个方向的所有卡片这三个向量必然完全相同), 跟 UE 自己的
    // 运行时 FLumenCard 是同一个思路。
    void getMeshCardDirectionAxes(uint8_t directionIndex, float3& outAxisX, float3& outAxisY, float3& outAxisZ) noexcept;
}
