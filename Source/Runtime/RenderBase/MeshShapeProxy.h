#pragma once
#include "MeshShapeTypes.h"
#include "ResourceProxy.h"
#include "DistanceFieldBrickAtlas.h"

#include <Core/Math/Box.h>
#include <Core/Math/Vector.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hyp
{
    // Mesh 资产级别(hyp::MeshShapeProxy)的数据在 RenderBackend::getMeshShapeDataTable() 里的定长头部——
    // 跟 Shader/Common/SparseDistanceField.slang 里的 SparseDistanceFieldObject 逐字段
    // 对应, 除了 indirectionTableOffset(不用存: 永远等于这个网格的区间起始偏移 + kMeshShapeMetadataWordCount)
    // 和 atlasBrickGridExtent(DistanceFieldBrickAtlas 级别的全局常量, 所有网格共用同一个值, 不用重复存)。
    // 没有 SDF 数据的网格(只有 card)这几个字段全 0——resolution 全 0 时间接索引表字数按公式算出来
    // 也恰好是 0, 天然表示"没有 brick"
    struct MeshShapeMetadata
    {
        float3 boundsMin;
        float3 boundsMax;
        float narrowBandDistance;
        int3 resolution;
        uint32_t cardCount;
    };
    inline constexpr uint32_t kMeshShapeMetadataWordCount = 11;
    static_assert(sizeof(MeshShapeMetadata) == kMeshShapeMetadataWordCount * sizeof(uint32_t), "MeshShapeMetadata 必须是 11 个 uint32 紧凑排列, 否则上传/采样两端手动按 11 个一组取值的约定会对不上");

    // CardLookup 紧跟在间接索引表后面, 固定 6 个 uint32——每个轴对齐方向一个位图, 第 i 位
    // 为 1 表示第 i 张卡(全局编号)属于这个方向, 没有 card 的网格全 0
    inline constexpr uint32_t kMeshShapeCardLookupWordCount = 6;

    // CardData 紧跟在 CardLookup 后面, 每张卡片定长 7 个 uint32(center.xyz/halfExtents.xyz
    // 的位模式 + directionIndex, 见 hyp::MeshCard), 张数是 MeshShapeMetadata::cardCount
    inline constexpr uint32_t kMeshShapeCardWordCount = 7;

    // Mesh 的粗粒度形状代理——距离场(稀疏体素)+ Mesh Card 列表, 都是 Mesh 资产级别的
    // 数据, 不属于任何具体场景实例; 也不像 GeometryProxy 那样按 LOD 建, 因为距离场/Card
    // 是全 LOD 共享的一份代理(见 Mesh::DistanceFieldData/CardData 的注释)。
    // 通过 Mesh::getOrCreateMeshShapeProxy() 创建。
    //
    // brick 体素数据本身不由这个类持有——实际存储是 RenderBackend::getMeshDistanceFieldBrickAtlas()
    // 管理的跨网格共享 3D 纹理, 这里只持有从那个池子申请到的槽位号列表(mBrickSlots)和
    // 一份"体素网格坐标 -> 槽位号"的间接索引表(已经把索引表里原本的"局部 brick 序号"
    // 重写成申请到的全局槽位号, 采样时不需要再做一次"局部->全局"的二次查找)。距离场元数据/
    // 间接索引表/Card 数据都是 RenderBackend::getMeshShapeDataTable() 那张跨网格共享表
    // 里的一段区间, 这个类自己不持有任何 GPU buffer。
    class MeshShapeProxy : public ResourceProxy
    {
    public:
        // 跟 Mesh::DistanceFieldData 逐字段对应的纯数据快照(不含 BulkData 这种延迟加载
        // 包装, 调用方在游戏线程读完 Mesh 私有数据后拷出这一份, 传给渲染线程构造)
        struct DistanceFieldInput
        {
            int3 resolution;
            Boxf bounds;
            float narrowBandDistance;
            int3 brickGridResolution;
            std::vector<uint32_t> brickIndirection; // 元素是局部 brick 序号(0..brickCount-1)或 kInvalidDistanceFieldBrickIndex, 构造时会被原地重写成全局槽位号
            std::vector<uint8_t> brickData; // 紧密排列的有效 brick, 每块 brickSize^3 个 uint8(R8_UNORM 量化编码), x 变化最快、然后 y、然后 z, 顺序对应 brickIndirection 里的局部序号
        };

        MeshShapeProxy(DistanceFieldInput&& dfInput, std::vector<MeshCard>&& cards);

        ~MeshShapeProxy() noexcept override;

        bool hasDistanceFieldData() const noexcept
        {
            return mHasDistanceFieldData;
        }

        int3 getDistanceFieldResolution() const noexcept
        {
            return mDistanceFieldResolution;
        }

        const Boxf& getDistanceFieldBounds() const noexcept
        {
            return mDistanceFieldBounds;
        }

        float getDistanceFieldNarrowBandDistance() const noexcept
        {
            return mDistanceFieldNarrowBandDistance;
        }

        int3 getDistanceFieldBrickGridResolution() const noexcept
        {
            return mDistanceFieldBrickGridResolution;
        }

        // 这个网格在 RenderBackend::getMeshShapeDataTable() 那张全局共享表里的区间
        // 起始下标——只有 mHasObjectTableEntry 为 true(距离场或 Card 至少有一样)时才有效,
        // 三个 offset 都从这里派生
        uint32_t getObjectTableOffset() const noexcept
        {
            return mObjectTableOffset;
        }

        // 间接索引表在共享表里的起始下标——永远等于 getObjectTableOffset() +
        // kMeshShapeMetadataWordCount(metadata 头紧跟在区间最前面), 没有距离场数据
        // 时长度是 0(见 mIndirectionTableCount), 这个 offset 本身仍然有效, 只是指向一段
        // 空区间
        uint32_t getIndirectionTableOffset() const noexcept
        {
            return mObjectTableOffset + kMeshShapeMetadataWordCount;
        }

        // CardLookup 在共享表里的起始下标——紧跟在间接索引表后面
        uint32_t getCardLookupOffset() const noexcept
        {
            return getIndirectionTableOffset() + mIndirectionTableCount;
        }

        // CardData 在共享表里的起始下标——紧跟在 CardLookup 后面
        uint32_t getCardDataOffset() const noexcept
        {
            return getCardLookupOffset() + kMeshShapeCardLookupWordCount;
        }

        uint32_t getCardCount() const noexcept
        {
            return static_cast<uint32_t>(mCards.size());
        }

        // CPU 端可读的 card 几何定义——跟 getCardDataOffset() 处共享表里那份是同一份数据
        // 的两种形态: GPU 那份是给采样阶段用的, 这个 span 是给需要在 CPU 上直接算东西的
        // 消费方用的(比如 MeshRenderProxy::collectCardRequests() 按 card 半宽 + 实例世界
        // 缩放算 card atlas 分辨率), 不用为了读几个 float 去发一次 GPU 回读
        std::span<const MeshCard> getCards() const noexcept
        {
            return mCards;
        }

    private:
        // 派生 MeshShapeDataTable 里这个网格总共占用的字数——只有 mHasObjectTableEntry
        // 为 true 时才有意义, 构造成功、析构归还两处都要用到, 不单独存一份, 现算
        uint32_t getObjectTableTotalWordCount() const noexcept;

    private:
        bool mHasDistanceFieldData = false;
        int3 mDistanceFieldResolution{};
        Boxf mDistanceFieldBounds;
        float mDistanceFieldNarrowBandDistance = 0.0f;
        int3 mDistanceFieldBrickGridResolution{};

        std::vector<uint32_t> mBrickSlots; // 从 DistanceFieldBrickAtlas 申请到的全局槽位号, 析构时要还回去

        bool mHasObjectTableEntry = false; // 有没有在共享表里分配区间(距离场或 Card 任一存在就是 true), 析构时靠它决定要不要归还
        uint32_t mObjectTableOffset = 0;
        uint32_t mIndirectionTableCount = 0; // 没有距离场数据时是 0

        std::vector<MeshCard> mCards;
    };

    template <>
    struct TProxyTraits<ProxyType::Mesh_Shape>
    {
        using Proxy = MeshShapeProxy;
    };
}
