#include "MeshShapeProxy.h"
#include "RenderBackend.h"
#include "DistanceFieldBrickAtlas.h"

#include <array>
#include <cassert>
#include <cstring>

namespace hyp
{
    MeshShapeProxy::MeshShapeProxy(DistanceFieldInput&& dfInput, std::vector<MeshCard>&& cards)
        : mCards(std::move(cards))
    {
        auto& renderBackend = getRenderBackend();
        DistanceFieldBrickAtlas& mdfBrickAtlas = renderBackend.getMeshDistanceFieldBrickAtlas();

        // 局部结果, 只有 MeshShapeDataTable 分配成功后才提交进成员变量——分配失败要能
        // 整体退化回"这个网格什么共享表数据都没有", 不留半提交状态
        bool distanceFieldReady = false;
        int3 distanceFieldResolution{};
        Boxf distanceFieldBounds;
        float distanceFieldNarrowBandDistance = 0.0f;
        int3 distanceFieldBrickGridResolution{};
        uint32_t indirectionCount = 0;

        // upload distance field data
        {
            const size_t brickCount = dfInput.brickData.size() / kDistanceFieldBrickVoxelCount;

            mBrickSlots = brickCount > 0 ? mdfBrickAtlas.allocate(static_cast<uint32_t>(brickCount)) : std::vector<uint32_t>{};

            // 池子容量不够时退化成"这个网格没有距离场"——不让一个网格的分配失败拖垮整个
            // 渲染, 调用方(未来的 shading pass)看 hasDistanceFieldData() 就知道要不要跳过
            if (brickCount == 0 || !mBrickSlots.empty())
            {
                distanceFieldReady = true;
                distanceFieldResolution = dfInput.resolution;
                distanceFieldBounds = dfInput.bounds;
                distanceFieldNarrowBandDistance = dfInput.narrowBandDistance;
                distanceFieldBrickGridResolution = dfInput.brickGridResolution;
                indirectionCount = static_cast<uint32_t>(dfInput.brickIndirection.size());

                // 把间接索引表里的"局部 brick 序号"原地重写成刚申请到的全局槽位号——
                // 采样时直接就是最终能用的槽位号, 不需要运行时再做一次二次查找
                for (uint32_t& entry : dfInput.brickIndirection)
                {
                    if (entry != kInvalidDistanceFieldBrickIndex)
                    {
                        assert(entry < mBrickSlots.size());
                        entry = mBrickSlots[entry];
                    }
                }

                if (brickCount > 0)
                {
                    mdfBrickAtlas.queueBrickUploads(mBrickSlots, std::move(dfInput.brickData));
                }
            }
        }

        const uint32_t cardCount = static_cast<uint32_t>(mCards.size());

        if (distanceFieldReady || cardCount > 0)
        {
            static_assert(sizeof(MeshCard) == kMeshShapeCardWordCount * sizeof(uint32_t), "MeshCard 必须是 7 个 uint32 紧凑排列, 否则下面的扫平写法和 shader 端手动按 7 个一组取值的约定会对不上");

            const uint32_t totalWordCount = kMeshShapeMetadataWordCount + indirectionCount
                + kMeshShapeCardLookupWordCount + cardCount * kMeshShapeCardWordCount;

            MeshShapeDataTable& objectTable = renderBackend.getMeshShapeDataTable();
            std::optional<uint32_t> objectTableOffset = objectTable.allocate(totalWordCount);

            // 共享表分配失败也要整体退化——不能让 brick 槽位分配成功了、却没有对应的
            // metadata/间接索引区间能查到它们, 那样这些槽位就是白白占用、永远用不上
            if (objectTableOffset)
            {
                mHasDistanceFieldData = distanceFieldReady;
                mDistanceFieldResolution = distanceFieldResolution;
                mDistanceFieldBounds = distanceFieldBounds;
                mDistanceFieldNarrowBandDistance = distanceFieldNarrowBandDistance;
                mDistanceFieldBrickGridResolution = distanceFieldBrickGridResolution;
                mHasObjectTableEntry = true;
                mObjectTableOffset = *objectTableOffset;
                mIndirectionTableCount = indirectionCount;

                std::vector<uint32_t> data(totalWordCount, 0u);

                // MeshShapeMetadata 是跟 shader 端逐字段对应的紧凑布局(见 MeshShapeProxy.h),
                // 要拆成 boundsMin/boundsMax 两个 float3, 不能直接存 Boxf
                MeshShapeMetadata metadata{ mDistanceFieldBounds.getMin(), mDistanceFieldBounds.getMax(), mDistanceFieldNarrowBandDistance, mDistanceFieldResolution, cardCount };
                std::memcpy(data.data(), &metadata, sizeof(metadata));

                if (indirectionCount > 0)
                {
                    std::memcpy(data.data() + kMeshShapeMetadataWordCount, dfInput.brickIndirection.data(), indirectionCount * sizeof(uint32_t));
                }

                const uint32_t cardLookupBase = kMeshShapeMetadataWordCount + indirectionCount;
                if (cardCount > 0)
                {
                    // CardLookup: 6 个 uint32 位图, 每个对应一个轴对齐方向, 第 i 位为 1
                    // 表示这个 mesh 的第 i 张卡(全局编号, 跨方向统一编号)属于这个方向——
                    // 采样阶段用命中法线选出 1~3 个方向, 直接查这几个位图就能拿到候选卡
                    // 集合, 不需要遍历全部卡片。单个 uint32 最多表示 32 张卡, 远够用
                    // (kMaxLumenMeshCards=12), 照抄 UE FLumenMeshCards::CardLookup 的思路
                    std::array<uint32_t, kMeshShapeCardLookupWordCount> cardLookup{};
                    for (uint32_t localCardIndex = 0; localCardIndex < cardCount; ++localCardIndex)
                    {
                        const uint8_t directionIndex = static_cast<uint8_t>(mCards[localCardIndex].directionIndex);
                        assert(directionIndex < kMeshShapeCardLookupWordCount);
                        assert(localCardIndex < 32 && "CardLookup 每个方向是一个 uint32 位图, 最多表示 32 张卡——远超 kMaxLumenMeshCards=12 的上限, 正常情况下不会触发");
                        cardLookup[directionIndex] |= (1u << localCardIndex);
                    }
                    std::memcpy(data.data() + cardLookupBase, cardLookup.data(), sizeof(cardLookup));

                    const uint32_t cardDataBase = cardLookupBase + kMeshShapeCardLookupWordCount;
                    std::memcpy(data.data() + cardDataBase, mCards.data(), cardCount * sizeof(MeshCard));
                }

                objectTable.queueUpload(mObjectTableOffset, std::move(data));
            }
            else if (!mBrickSlots.empty())
            {
                mdfBrickAtlas.free(mBrickSlots);
                mBrickSlots.clear();
            }
        }
    }

    MeshShapeProxy::~MeshShapeProxy() noexcept
    {
        auto& renderBackend = getRenderBackend();

        if (!mBrickSlots.empty())
        {
            // 归还槽位号跟销毁 GPU 资源一样要等 kMaxFramesInFlight 帧之后——这一帧可能还有
            // 别的正在飞行中的渲染指令在读这些槽位对应的 atlas 数据, 立即归还的话槽位可能
            // 马上被别的网格重新分配、把内容改掉, 读到的就是错误数据
            renderBackend.getDeferredTaskQueue().push(
                [&renderBackend, slots = std::move(mBrickSlots)]()
                {
                    renderBackend.getMeshDistanceFieldBrickAtlas().free(slots);
                },
                kMaxFramesInFlight
            );
        }

        if (mHasObjectTableEntry)
        {
            renderBackend.getDeferredTaskQueue().push(
                [&renderBackend, offset = mObjectTableOffset, count = getObjectTableTotalWordCount()]()
                {
                    renderBackend.getMeshShapeDataTable().free(offset, count);
                },
                kMaxFramesInFlight
            );
        }
    }

    uint32_t MeshShapeProxy::getObjectTableTotalWordCount() const noexcept
    {
        return kMeshShapeMetadataWordCount + mIndirectionTableCount
            + kMeshShapeCardLookupWordCount + static_cast<uint32_t>(mCards.size()) * kMeshShapeCardWordCount;
    }
}
