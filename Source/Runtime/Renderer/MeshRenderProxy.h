#pragma once
#include "RenderData.h"
#include "SceneView.h"
#include "DrawableProxy.h"
#include "SkinningManager.h"
#include "SurfaceCachePageAllocator.h"

#include <RenderBase/RenderBaseDefines.h>

#include <Core/Math/Box.h>
#include <Core/Math/Vector.h>
#include <Core/Math/Matrix.h>

#include <optional>
#include <span>
#include <vector>

namespace hyp
{
    class MeshShapeProxy;
    class MeshRenderProxy;

    // 一张卡想从当前已分配的分辨率切换到 resolution
    struct SurfaceCacheCardRequest
    {
        MeshRenderProxy* proxy;
        uint32_t cardIndex;
        int32_t resolution;
        float viewerDistance;
    };

    enum class SurfaceCacheCardRequestResult
    {
        Applied,
        Deferred, // 临时捕获图集放不下, 这张卡保持旧分配
        Failed    // 物理图集或页表放不下, 这张卡现在没有页
    };

    // 一张卡重分配后其中一页需要捕获: 画进临时捕获图集的 captureRegion, 再拷到物理图集的 atlasRegion
    struct SurfaceCachePageCapture
    {
        MeshRenderProxy* proxy;
        uint32_t cardIndex;
        int2 pageIndex;
        int2 pageCount;
        SurfaceCachePageRegion captureRegion;
        SurfaceCachePageRegion atlasRegion;
        bool isNewAllocation; // 新分配的页才需要重置光照, 刷新和失效的页保留原有光照
    };

    // 一张卡的光照更新状态, 由 LumenScene::updateLightingSchedule() 维护
    struct SurfaceCacheCardLightingState
    {
        float viewerDistance = 0.0f;
        uint32_t lastDirectUpdateFrame = 0;
        uint32_t lastIndirectUpdateFrame = 0;
        uint32_t indirectTemporalIndex = 0;
    };

    class MeshRenderProxy : public DrawableProxy
    {
    public:
        using Desc = DrawableProxy::Desc;

        MeshRenderProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~MeshRenderProxy() noexcept override;

        void setBoundingBox(Boxd boundingBox) noexcept override;

        // mobility 真的变化时, 要把这个 proxy 的包围盒从旧分组挪到新分组(两边都标脏)
        void setMobility(Mobility mobility) noexcept override;

        struct LODRange
        {
            uint32_t startItem;
            uint32_t itemCount;
            float minScreenPercentage;
        };
        void setDrawItems(std::vector<DrawItem>&& drawItems, std::vector<LODRange>&& lodRanges) noexcept
        {
            mDrawItems = std::move(drawItems);
            mLodRanges = std::move(lodRanges);
            invalidateCards();
            rebuildSections();
        }

        std::span<const DrawItem> getDrawItems(const SceneView& view) const noexcept override
        {
            return getDrawItemsLOD0();
        }

        // LOD 选择目前(见 getDrawItems)恒定是 LOD0, 没有真的按 view 算——给不需要
        // SceneView 的消费方(比如 CardCapturePass, 它的"视角"是 card 自己的正交投影,
        // 不是场景相机, 构造一个 SceneView 纯粹是为了凑参数)一个不用假装有 view 的入口
        std::span<const DrawItem> getDrawItemsLOD0() const noexcept
        {
            if (mLodRanges.empty())
                return {};

            auto lodRange = mLodRanges[0];
            return { &mDrawItems[lodRange.startItem], lodRange.itemCount };
        }

        // Mesh 资产级别的形状代理(距离场 + Mesh Card), 由 StaticMeshRenderer 在
        // updateProxyDrawItems() 里解析对应 Mesh 的句柄后填入。没有 Mesh(比如
        // Inspector 里把 Mesh 重置成 None)时保持 nullptr——所有消费方(未来画场景
        // SDF 的 Pass)都必须自己判空跳过, 不能假设它一定存在
        //
        // 换成不同的 MeshShapeProxy(包括换成 nullptr)之前必须先释放旧 card 页——旧
        // ranges 记录的是旧 mesh 的卡片数量, 对新 mesh 完全没有意义
        void setMeshShapeProxy(MeshShapeProxy* meshShapeProxy) noexcept;

        MeshShapeProxy* getMeshShapeProxy() const noexcept
        {
            return mMeshShapeProxy;
        }

        // 这个实例在 SceneProxy 逐 mesh 实例表(见 SceneProxyData.h 里 MeshInstanceData 的
        // 注释)里的下标——跟 getInstanceIndex() 一样, 构造时分配、析构时归还, 恒有效, 不需要像
        // mMeshShapeProxy/mCardRangeSlotBase 那样区分"有没有"。GlobalDistanceField 的 Object
        // Grid 存的就是这个下标(不是 getInstanceIndex() 那个通用场景下标), 命中反查阶段靠它去查
        // meshShapeDataOffset/cardRangeSlotBase
        uint32_t getMeshInstanceIndex() const noexcept
        {
            return mMeshInstanceIndex;
        }

        // ----------------------------- Lumen Card Capture 相关 -----------------------------

        // 按世界变换和到全部相机的最近距离算每张 card 的期望分辨率(公式见 .cpp),
        // 与当前已分配分辨率不同的产出一个请求; 必要时先分配该实例的 ranges 区间
        void collectCardRequests(
            const WorldMatrix& objectToWorld,
            std::span<const double3> cameraWorldPositions,
            std::vector<SurfaceCacheCardRequest>& requests);

        // 先在临时捕获图集里占位, 再释放这张卡的旧页并按 resolution 在物理图集里重新分配;
        // Applied 时把需要捕获的各页追加到 captures
        SurfaceCacheCardRequestResult applyCardRequest(
            uint32_t cardIndex,
            int32_t resolution,
            SurfaceCachePageAllocator& captureAllocator,
            std::vector<SurfaceCachePageCapture>& captures);

        SurfaceCachePageRegion getCardPageRegion(uint32_t cardIndex, uint32_t pageLinearIndex) const;

        // 在临时捕获图集里占位, 成功则把这一页追加到 captures(不重置光照); 临时捕获图集放不下返回 false,
        // 实例没有可画内容时视为已处理并返回 true
        bool recapturePage(
            uint32_t cardIndex,
            uint32_t pageLinearIndex,
            SurfaceCachePageAllocator& captureAllocator,
            std::vector<SurfaceCachePageCapture>& captures);

        SurfaceCacheCardLightingState& getCardLightingState(uint32_t cardIndex) noexcept
        {
            return mCardLightingStates[cardIndex];
        }

        // 把全部驻留页推入重捕获队列, 没有驻留页时是 no-op
        void invalidateCards() noexcept;

        // 采样阶段(MeshSDFScene.slang)通过 push constant 里的 cardRangeSlotBase 字段拿到
        // 这个值——只有 hasCardRanges() 为 true 时才有意义
        uint32_t getCardRangeSlotBase() const noexcept
        {
            return mCardRangeSlotBase;
        }

        // 这个实例的 ranges 区间是否存在, 区间存在不代表每张卡都有页(页数为 (0,0) 的卡由消费方跳过)。
        // false 时 mCardRangeSlotBase 是陈旧值, 消费方必须先检查这个再读它
        bool hasCardRanges() const noexcept
        {
            return mHasCardRanges;
        }

        // 释放这个实例名下的 card ranges/payload/物理页, 没有 mHasCardRanges 时是 no-op
        void freeCardPages() noexcept;

        // ----------------------------- Skeletal Mesh Skinning 相关 -----------------------------

        // 分配失败(矩阵表容量耗尽)时 MeshInstanceData 里的区间保持 kInvalidBoneMatrixRangeBase,
        // shader 按单位矩阵画 bind pose, 调用方不需要特殊处理这种降级; boneCount 为 0 时是 no-op
        void allocateSkinning(uint32_t boneCount);

        // 没有分配过时是 no-op
        void freeSkinning() noexcept;

        // 没有分配过时是 no-op
        void updateSkinningMatrices(std::vector<float3x4>&& matrices);

        // 姿势跳变时用, 避免 TAA 把这次不连续当成一帧内的真实运动, 没有分配过时是 no-op
        void markSkinningDiscontinuous() noexcept;

    private:
        // 按 LOD0 的 draw item 重建 section 表区间, section 下标即 draw item 在 LOD0 里的下标
        void rebuildSections();

        // 没有 mHasSections 时是 no-op
        void freeSections() noexcept;

        // 为全部 card 分配 ranges 区间并初始化为 (0,0); 共享表空间不足时返回 false
        bool allocateCardRanges(uint32_t cardCount);

    private:
        std::vector<DrawItem> mDrawItems;
        std::vector<LODRange> mLodRanges;
        MeshShapeProxy* mMeshShapeProxy = nullptr;
        uint32_t mMeshInstanceIndex;

        uint32_t mCardRangeSlotBase = 0; // 这个实例的 ranges 记录在 SurfaceCacheAtlas 共享表里的起始下标, 只在 mHasCardRanges 为 true 时有意义
        bool mHasCardRanges = false;

        // 每张卡当前已分配的分辨率(texel, 不是页数), 0 表示没有页; 只在 mHasCardRanges 为 true 时有意义
        std::vector<int32_t> mCardResolutions;

        // 每张卡在 SurfaceCacheRecaptureQueue 里的记录起始 id, 只在对应 mCardResolutions 非 0 时有意义
        std::vector<uint32_t> mCardRecordBases;

        // 只在 mHasCardRanges 为 true 时有意义, 大小等于 mCardResolutions
        std::vector<SurfaceCacheCardLightingState> mCardLightingStates;

        std::optional<SkinningHandle> mSkinning;
        uint32_t mSectionTableBase = 0;
        uint32_t mSectionCount = 0;
        bool mHasSections = false;
    };

    template <>
    struct TProxyTraits<ProxyType::Mesh_Render>
    {
        using Proxy = MeshRenderProxy;
    };
}
