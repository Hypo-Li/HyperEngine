#pragma once
#include "SurfaceCachePageAllocator.h"

#include <RenderBase/StagingAllocator.h>

#include <Core/Common/Refable.h>
#include <Core/Container/RangeAllocator.h>
#include <Core/Math/Vector.h>
#include <RHI/Buffer.h>
#include <RHI/Texture.h>
#include <RHI/CommandList.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hyp
{
    class RenderBackend;

    // 每轴 texel 数, 与 MeshShapeCard.slang 中的同名常量保持一致
    inline constexpr int32_t kSurfaceCacheAtlasResolution = 2048;
    static_assert(kSurfaceCacheAtlasResolution % kSurfaceCachePageSize == 0, "分辨率必须是页尺寸的整数倍");

    inline constexpr int32_t kSurfaceCacheAtlasPagesPerAxis = kSurfaceCacheAtlasResolution / kSurfaceCachePageSize;
    inline constexpr uint32_t kSurfaceCacheAtlasPageCapacity = static_cast<uint32_t>(kSurfaceCacheAtlasPagesPerAxis * kSurfaceCacheAtlasPagesPerAxis);

    inline constexpr uint32_t kInvalidSurfaceCacheAtlasPageIndex = 0xFFFFFFFFu;

    // 每个 card tile 配一个 Radiosity 累积帧计数, 最小子页恰为一个 tile
    inline constexpr int32_t kSurfaceCacheCardTileSize = kSurfaceCacheSubPageMinSize;

    // 对应 UE r.LumenScene.SurfaceCache.CardMaxResolution
    inline constexpr int32_t kSurfaceCacheCardMaxResolution = 512;

    // 全图集都切成最小子页时的 card page 个数
    inline constexpr uint32_t kSurfaceCacheAtlasMaxCardPages = static_cast<uint32_t>(
        (kSurfaceCacheAtlasResolution / kSurfaceCacheSubPageMinSize) * (kSurfaceCacheAtlasResolution / kSurfaceCacheSubPageMinSize));

    // CPU 与 GPU(MeshSDFScene.slang)共用的 card page table 记录布局
    struct CardPageRange
    {
        uint32_t pageIndirectionOffset;
        int2 pageCount;
    };
    static_assert(sizeof(CardPageRange) == 3 * sizeof(uint32_t), "CardPageRange 必须是 3 个 uint32 紧凑排列, 否则上传/采样两端手动按 3 个一组取值的约定会对不上");

    // 每个 card page 配 1 条 payload 和 1 条 ranges 记录, 分配失败的卡只有记录没有页, 仅在饱和时出现
    inline constexpr uint32_t kSurfaceCacheAtlasCardPageTableCapacity =
        kSurfaceCacheAtlasMaxCardPages * (1u + static_cast<uint32_t>(sizeof(CardPageRange) / sizeof(uint32_t)));

    // 场景内共享的 Surface Cache 页池, 同时持有 card page table
    class SurfaceCacheAtlas
    {
    public:
        SurfaceCacheAtlas(RenderBackend& renderBackend);

        ~SurfaceCacheAtlas();

        SurfaceCacheAtlas(const SurfaceCacheAtlas&) = delete;
        SurfaceCacheAtlas& operator=(const SurfaceCacheAtlas&) = delete;

        // 总是改写该卡的 range 记录: 成功写入新页并返回各页在图集里的矩形(行优先), 失败写 (0,0) 并返回空
        std::vector<SurfaceCachePageRegion> allocateCard(uint32_t rangeSlotBase, uint32_t cardIndex, int32_t resolution);

        // 不改写 range 记录, 调用方须随后 allocateCard() 或释放整个 ranges 区间
        void freeCard(uint32_t rangeSlotBase, uint32_t cardIndex) noexcept;

        rhi::TextureView& getAlbedoAtlasTextureView() const noexcept
        {
            return *mAlbedoAtlasView;
        }

        rhi::TextureView& getDepthAtlasTextureView() const noexcept
        {
            return *mDepthAtlasView;
        }

        rhi::TextureView& getNormalAtlasTextureView() const noexcept
        {
            return *mNormalAtlasView;
        }

        rhi::TextureView& getEmissiveAtlasTextureView() const noexcept
        {
            return *mEmissiveAtlasView;
        }

        rhi::TextureView& getDirectLightingAtlasTextureView() const noexcept
        {
            return *mDirectLightingAtlasView;
        }

        rhi::TextureView& getIndirectLightingAtlasTextureView() const noexcept
        {
            return *mIndirectLightingAtlasView;
        }

        rhi::TextureView& getFinalLightingAtlasTextureView() const noexcept
        {
            return *mFinalLightingAtlasView;
        }

        rhi::TextureView& getRadiosityNumFramesAccumulatedAtlasTextureView() const noexcept
        {
            return *mRadiosityNumFramesAccumulatedAtlasView;
        }

        // 返回 nullopt 可能只是碎片化, 不代表总空闲量不足
        std::optional<uint32_t> allocateCardPageTableSlots(uint32_t uint32Count);

        void freeCardPageTableSlots(uint32_t offset, uint32_t uint32Count) noexcept;

        // 写入 CPU 副本并标记脏区间, 由 flushCardPageTableUploads() 合并后分帧上传
        void queueCardPageTableUpload(uint32_t offset, std::span<const uint32_t> data);

        // 首次调用把全部图集清零并转到 Shader_Sampled, 须早于任何使用这些图集的 pass
        void prepare(rhi::CommandList& cmdList);

        // 须在本帧全部 allocateCard()/freeCard() 之后调用, 否则新写入会晚一帧才上传
        void flushCardPageTableUploads(rhi::CommandList& cmdList);

        rhi::Buffer* getCardPageTableBuffer() const noexcept
        {
            return mCardPageTableBuffer.get();
        }

        // CPU 侧常驻副本, 由 queueCardPageTableUpload() 同步更新
        std::span<const uint32_t> getCardPageTable() const noexcept
        {
            return mCardPageTable;
        }

        // 该卡第 pageLinearIndex 页(行优先)在图集里的矩形, 要求这张卡当前有页
        SurfaceCachePageRegion getCardPageRegion(uint32_t rangeSlotBase, uint32_t cardIndex, uint32_t pageLinearIndex) const noexcept;

        // rangeSlotBase 越界或该位置从未写入时行为未定义
        CardPageRange getCardPageRange(uint32_t rangeSlotBase, uint32_t cardIndex) const noexcept
        {
            const uint32_t base = rangeSlotBase + cardIndex * 3u;
            CardPageRange range;
            range.pageIndirectionOffset = mCardPageTable[base + 0];
            range.pageCount = int2(int32_t(mCardPageTable[base + 1]), int32_t(mCardPageTable[base + 2]));
            return range;
        }

    private:
        RenderBackend& mRenderBackend;

        TRef<rhi::Texture> mAlbedoAtlas;
        TRef<rhi::TextureView> mAlbedoAtlasView;

        TRef<rhi::Texture> mDepthAtlas;
        TRef<rhi::TextureView> mDepthAtlasView;

        TRef<rhi::Texture> mNormalAtlas;
        TRef<rhi::TextureView> mNormalAtlasView;

        TRef<rhi::Texture> mEmissiveAtlas;
        TRef<rhi::TextureView> mEmissiveAtlasView;

        // 由 compute shader 直接 Store() 写入, 所以 usage 为 Storage 而非 Color_Attachment
        TRef<rhi::Texture> mDirectLightingAtlas;
        TRef<rhi::TextureView> mDirectLightingAtlasView;

        // 常驻是因为它本身就是多次反弹闭环的时域历史
        TRef<rhi::Texture> mIndirectLightingAtlas;
        TRef<rhi::TextureView> mIndirectLightingAtlasView;

        TRef<rhi::Texture> mFinalLightingAtlas;
        TRef<rhi::TextureView> mFinalLightingAtlasView;

        // 每个 card tile 一格, 见 kSurfaceCacheCardTileSize
        TRef<rhi::Texture> mRadiosityNumFramesAccumulatedAtlas;
        TRef<rhi::TextureView> mRadiosityNumFramesAccumulatedAtlasView;

        bool mInitialized = false;

        SurfaceCachePageAllocator mPageAllocator;

        RangeAllocator mCardPageTableAllocator;
        TRef<rhi::Buffer> mCardPageTableBuffer;
        std::vector<uint32_t> mCardPageTable;

        struct DirtyRange
        {
            uint32_t begin; // uint32 为单位, 左闭右开
            uint32_t end;
        };
        std::vector<DirtyRange> mDirtyCardPageTableRanges;
    };
}
