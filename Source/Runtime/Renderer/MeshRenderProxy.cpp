#include "MeshRenderProxy.h"
#include "SurfaceCacheAtlas.h"
#include "GlobalDistanceField.h"
#include "LumenScene.h"
#include "SceneProxy.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/MaterialProxy.h>
#include <RenderBase/MeshShapeProxy.h>

#include <Core/Common/LogSystem.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <optional>
#include <limits>

namespace hyp
{
    namespace
    {
        // 分辨率计算照抄 UE Lumen Surface Cache 的公式(LumenSceneRendering.cpp 的
        // GetCardTexelDensity/逐 card 分辨率计算): 一张 card 的正方形边长(texel) =
        //   clamp(min(TexelDensityScale * CardExtent / ViewerDistance,
        //             MaxTexelDensity * CardExtent),
        //         MinResolution, MaxResolution)
        //   再 RoundUpToPowerOfTwo
        // CardExtent/ViewerDistance 相除是无量纲比值, TexelDensityScale 不受长度单位影响,
        // UE 的默认值 100 直接照搬。MaxTexelDensity 是"每单位距离多少 texel"的密度常量,
        // 有量纲, UE 默认 0.2 是"每厘米"(UE 世界单位是厘米), 这里引擎用米作单位, 换算成
        // "每米" 要乘 100(1 米 = 100 厘米), 即 0.2 * 100 = 20
        constexpr float kCardAtlasTexelDensity = 100.0f;
        constexpr float kCardMaxTexelDensityPerMeter = 20.0f;
        constexpr int32_t kCardMinResolution = 8;   // 对应 UE Lumen::MinCardResolution

        uint32_t getCardPageCount(int32_t resolution) noexcept
        {
            const int32_t pageCountPerAxis = std::max(1, (resolution + kSurfaceCachePageSize - 1) / kSurfaceCachePageSize);
            return static_cast<uint32_t>(pageCountPerAxis * pageCountPerAxis);
        }

        uint32_t roundUpToPowerOfTwo(uint32_t value) noexcept
        {
            if (value <= 1)
                return 1;
            --value;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
            return value + 1;
        }

        // cardExtent: card 面内两条半轴里较长的一条(世界空间, 米), 对应 UE 的
        // FLumenCardCullingInfo::GetMaxCardExtent()——跟 UE 一样把 card 当正方形处理
        // (UE 后续还会按长宽比单独给短轴打折扣, 见 FLumenCard::ComputeAndSetResLevelXYBias,
        // 我们目前没有做这一步, 短轴跟长轴用同一个分辨率, 这不是这次改动的范围)。
        // viewerDistance: 到主相机的世界空间距离(米), 提前 clamp 到一个很小的正数下限,
        // 避免相机穿进网格内部时除以 0。返回值是实际的目标分辨率(texel, 2 的幂)本身,
        // 不是页数——分辨率折算成几个页(以及是不是能塞进 SurfaceCacheAtlas 的子页 bin)
        // 交给 SurfaceCacheAtlas::allocateCard() 决定, 这里不提前折算掉
        int32_t computeCardResolution(float cardExtent, float viewerDistance) noexcept
        {
            const float safeDistance = std::max(viewerDistance, 0.01f);
            const float projSize = std::min(
                kCardAtlasTexelDensity * cardExtent / safeDistance,
                kCardMaxTexelDensityPerMeter * cardExtent
            );
            const float clampedSize = std::clamp(projSize, float(kCardMinResolution), float(kSurfaceCacheCardMaxResolution));
            return static_cast<int32_t>(roundUpToPowerOfTwo(static_cast<uint32_t>(clampedSize)));
        }
    }

    MeshRenderProxy::MeshRenderProxy(SceneProxy& sceneProxy, const Desc& desc) :
        DrawableProxy(sceneProxy, desc),
        mMeshInstanceIndex(sceneProxy.acquireMeshInstanceIndex())
    {
        sceneProxy.registerElement(this);
        sceneProxy.getMeshInstanceData(mMeshInstanceIndex) = MeshInstanceData{
            .instanceIndex = getInstanceIndex(),
            .meshShapeDataOffset = 0,
            .cardRangeSlotBase = kGlobalDistanceFieldInvalidCardRangeSlotBase,
            .boneMatrixRangeBase = kInvalidBoneMatrixRangeBase,
            .prevBoneMatrixRangeBase = kInvalidBoneMatrixRangeBase,
            .sectionTableBase = kInvalidSectionTableBase
        };
        sceneProxy.markMeshInstanceDataUpdated(mMeshInstanceIndex);
        sceneProxy.markMeshBoundsDirty(getMobility());
        sceneProxy.markGlobalDistanceFieldDirty(getBoundingBox(), getMobility());
    }

    MeshRenderProxy::~MeshRenderProxy() noexcept
    {
        freeSections();
        freeCardPages();
        freeSkinning();

        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.markMeshBoundsDirty(getMobility());
        sceneProxy.markGlobalDistanceFieldDirty(getBoundingBox(), getMobility());
        sceneProxy.unregisterElement(this);
        sceneProxy.releaseMeshInstanceIndex(mMeshInstanceIndex);
    }

    bool MeshRenderProxy::allocateCardRanges(uint32_t cardCount)
    {
        SceneProxy& sceneProxy = getSceneProxy();
        SurfaceCacheAtlas& surfaceCacheAtlas = sceneProxy.getLumenScene()->getSurfaceCacheAtlas();

        auto rangeSlotBase = surfaceCacheAtlas.allocateCardPageTableSlots(cardCount * 3u);
        if (!rangeSlotBase)
        {
            LOG_WARN("MeshRenderProxy::allocateCardRanges: instance={} ranges 表分配失败(共享表区间不够), 下一帧自动重试", getInstanceIndex());
            return false;
        }

        mCardRangeSlotBase = *rangeSlotBase;
        surfaceCacheAtlas.queueCardPageTableUpload(mCardRangeSlotBase, std::vector<uint32_t>(cardCount * 3u, 0u));
        mCardResolutions.assign(cardCount, 0);
        mCardRecordBases.assign(cardCount, 0u);
        mCardLightingStates.assign(cardCount, SurfaceCacheCardLightingState{});
        mHasCardRanges = true;

        sceneProxy.getMeshInstanceData(mMeshInstanceIndex).cardRangeSlotBase = mCardRangeSlotBase;
        sceneProxy.markMeshInstanceDataUpdated(mMeshInstanceIndex);
        return true;
    }

    void MeshRenderProxy::collectCardRequests(
        const WorldMatrix& objectToWorld,
        std::span<const double3> cameraWorldPositions,
        std::vector<SurfaceCacheCardRequest>& requests)
    {
        MeshShapeProxy* meshShapeProxy = mMeshShapeProxy;
        if (!meshShapeProxy)
        {
            freeCardPages();
            return;
        }

        assert(!cameraWorldPositions.empty() && "至少要有一个相机才能决定 card 分辨率, 调用方在没有相机时应该整体跳过这一帧");

        std::span<const MeshCard> cards = meshShapeProxy->getCards();

        LumenScene* lumenScene = getSceneProxy().getLumenScene();
        if (cards.empty() || !lumenScene)
        {
            freeCardPages();
            return;
        }

        if (!mHasCardRanges && !allocateCardRanges(static_cast<uint32_t>(cards.size())))
            return;

        // basis 每一列的长度就是对应物体轴的世界缩放系数
        const float3x3 basis = objectToWorld.getBasis();
        const float3 worldScale(glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2]));
        const double3 worldTranslation = objectToWorld.getTranslation().toDouble3();

        for (size_t cardIndex = 0; cardIndex < cards.size(); ++cardIndex)
        {
            const MeshCard& card = cards[cardIndex];

            float3 axisX, axisY, axisZ;
            getMeshCardDirectionAxes(static_cast<uint8_t>(card.directionIndex), axisX, axisY, axisZ);

            // axisX/axisY 是局部空间的 one-hot 基向量, dot 直接取出对应那一维
            const float worldHalfSizeX = glm::dot(axisX, card.halfExtents) * glm::dot(axisX, worldScale);
            const float worldHalfSizeY = glm::dot(axisY, card.halfExtents) * glm::dot(axisY, worldScale);
            const float cardExtent = std::max(worldHalfSizeX, worldHalfSizeY);

            const double3 cardWorldPos = worldTranslation + double3(basis * card.center);

            // SurfaceCacheAtlas 跨相机共享, 分辨率由离这张卡最近的相机决定
            float viewerDistance = std::numeric_limits<float>::max();
            for (const double3& cameraWorldPos : cameraWorldPositions)
            {
                viewerDistance = std::min(viewerDistance, float(glm::length(cardWorldPos - cameraWorldPos)));
            }

            mCardLightingStates[cardIndex].viewerDistance = viewerDistance;

            const int32_t resolution = computeCardResolution(cardExtent, viewerDistance);
            if (resolution != mCardResolutions[cardIndex])
                requests.push_back(SurfaceCacheCardRequest{ this, static_cast<uint32_t>(cardIndex), resolution, viewerDistance });
        }
    }

    SurfaceCacheCardRequestResult MeshRenderProxy::applyCardRequest(
        uint32_t cardIndex,
        int32_t resolution,
        SurfaceCachePageAllocator& captureAllocator,
        std::vector<SurfaceCachePageCapture>& captures)
    {
        assert(mHasCardRanges && "请求由 collectCardRequests() 产生, 那时 ranges 区间已经存在");

        std::vector<SurfaceCachePageAllocation> capturePages = captureAllocator.allocate(resolution, resolution);
        if (capturePages.empty())
            return SurfaceCacheCardRequestResult::Deferred;

        LumenScene* lumenScene = getSceneProxy().getLumenScene();
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();
        SurfaceCacheRecaptureQueue& recaptureQueue = lumenScene->getSurfaceCacheRecaptureQueue();

        const int32_t pageCountPerAxis = std::max(1, (resolution + kSurfaceCachePageSize - 1) / kSurfaceCachePageSize);
        const uint32_t pageCount = static_cast<uint32_t>(pageCountPerAxis * pageCountPerAxis);

        std::optional<uint32_t> recordBase = recaptureQueue.addCard(this, cardIndex, pageCount, lumenScene->getSurfaceCacheUpdateFrameIndex());
        if (!recordBase)
        {
            captureAllocator.free(capturePages);
            return SurfaceCacheCardRequestResult::Failed;
        }

        if (mCardResolutions[cardIndex] != 0)
        {
            recaptureQueue.removeCard(mCardRecordBases[cardIndex], getCardPageCount(mCardResolutions[cardIndex]));
            surfaceCacheAtlas.freeCard(mCardRangeSlotBase, cardIndex);
        }

        std::vector<SurfaceCachePageRegion> atlasRegions = surfaceCacheAtlas.allocateCard(mCardRangeSlotBase, cardIndex, resolution);
        if (atlasRegions.empty())
        {
            recaptureQueue.removeCard(*recordBase, pageCount);
            mCardResolutions[cardIndex] = 0;
            captureAllocator.free(capturePages);
            return SurfaceCacheCardRequestResult::Failed;
        }
        mCardResolutions[cardIndex] = resolution;
        mCardRecordBases[cardIndex] = *recordBase;

        // 新页没有任何光照历史, 优先级最高
        SurfaceCacheCardLightingState& lightingState = mCardLightingStates[cardIndex];
        lightingState.lastDirectUpdateFrame = 0;
        lightingState.lastIndirectUpdateFrame = 0;
        lightingState.indirectTemporalIndex = 0;

        assert(atlasRegions.size() == capturePages.size());
        for (size_t i = 0; i < atlasRegions.size(); ++i)
        {
            const int32_t pageIndex = static_cast<int32_t>(i);
            captures.push_back(SurfaceCachePageCapture{
                this,
                cardIndex,
                int2(pageIndex % pageCountPerAxis, pageIndex / pageCountPerAxis),
                int2(pageCountPerAxis, pageCountPerAxis),
                captureAllocator.getRegion(capturePages[i]),
                atlasRegions[i],
                true
            });
        }
        return SurfaceCacheCardRequestResult::Applied;
    }

    SurfaceCachePageRegion MeshRenderProxy::getCardPageRegion(uint32_t cardIndex, uint32_t pageLinearIndex) const
    {
        const SurfaceCacheAtlas& surfaceCacheAtlas = getSceneProxy().getLumenScene()->getSurfaceCacheAtlas();
        return surfaceCacheAtlas.getCardPageRegion(mCardRangeSlotBase, cardIndex, pageLinearIndex);
    }

    bool MeshRenderProxy::recapturePage(
        uint32_t cardIndex,
        uint32_t pageLinearIndex,
        SurfaceCachePageAllocator& captureAllocator,
        std::vector<SurfaceCachePageCapture>& captures)
    {
        assert(mCardResolutions[cardIndex] != 0 && "队列里只登记有页的卡");

        // 没有可画的内容时重捕获会把这一页冲成空白, 当作已处理直接跳过
        if (getDrawItemsLOD0().empty())
            return true;

        const SurfaceCachePageRegion atlasRegion = getCardPageRegion(cardIndex, pageLinearIndex);
        std::vector<SurfaceCachePageAllocation> capturePages = captureAllocator.allocate(atlasRegion.pixelExtent.x, atlasRegion.pixelExtent.y);
        if (capturePages.empty())
            return false;

        const int32_t pageCountPerAxis = std::max(1, (mCardResolutions[cardIndex] + kSurfaceCachePageSize - 1) / kSurfaceCachePageSize);
        const int32_t pageIndex = static_cast<int32_t>(pageLinearIndex);
        captures.push_back(SurfaceCachePageCapture{
            this,
            cardIndex,
            int2(pageIndex % pageCountPerAxis, pageIndex / pageCountPerAxis),
            int2(pageCountPerAxis, pageCountPerAxis),
            captureAllocator.getRegion(capturePages.front()),
            atlasRegion,
            false
        });
        return true;
    }

    void MeshRenderProxy::invalidateCards() noexcept
    {
        if (!mHasCardRanges)
            return;

        LumenScene* lumenScene = getSceneProxy().getLumenScene();
        if (!lumenScene)
            return;

        SurfaceCacheRecaptureQueue& recaptureQueue = lumenScene->getSurfaceCacheRecaptureQueue();
        for (size_t cardIndex = 0; cardIndex < mCardResolutions.size(); ++cardIndex)
        {
            if (mCardResolutions[cardIndex] != 0)
                recaptureQueue.invalidateCard(mCardRecordBases[cardIndex], getCardPageCount(mCardResolutions[cardIndex]));
        }
    }

    void MeshRenderProxy::setMeshShapeProxy(MeshShapeProxy* meshShapeProxy) noexcept
    {
        if (meshShapeProxy == mMeshShapeProxy)
            return;

        // 旧 mesh 的 card ranges(数量、几何都是按旧 mesh 算的)对新 mesh 完全没有意义,
        // 必须在切换之前释放, 见 MeshRenderProxy.h 里 setMeshShapeProxy() 的注释
        freeCardPages();
        mMeshShapeProxy = meshShapeProxy;

        // 只更新 mesh 实例表里 meshShapeDataOffset 这一个字段——表条目本身跟 mInstanceIndex
        // 同一时机在构造里分配好了, 这里不用申请/归还。换成空
        // mesh 时置 0(没有意义的占位值, 消费方按 Object Grid 里还查不查得到这个候选来决定
        // 用不用这份数据, 不靠这个字段本身的值判断有效性)
        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.getMeshInstanceData(mMeshInstanceIndex).meshShapeDataOffset =
            meshShapeProxy ? meshShapeProxy->getObjectTableOffset() : 0;
        sceneProxy.markMeshInstanceDataUpdated(mMeshInstanceIndex);

        // 这个实例刚拿到(或失去)一份可用的 SDF 数据, 影响它在 GDF 候选剔选里的归属——
        // 跟 setBoundingBox()/setMobility() 同样的理由标脏, 否则 dirty 已经在更早的一帧
        // 被空候选消费掉的话, updateCascadeLayer() 永远不会重新扫到这个实例
        sceneProxy.markGlobalDistanceFieldDirty(getBoundingBox(), getMobility());
    }

    void MeshRenderProxy::freeCardPages() noexcept
    {
        if (!mHasCardRanges)
            return;

        SceneProxy& sceneProxy = getSceneProxy();
        LumenScene* lumenScene = sceneProxy.getLumenScene();
        assert(lumenScene && "mHasCardRanges 为 true 意味着 allocateCardRanges() 里读到过非空的 LumenScene");

        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();
        SurfaceCacheRecaptureQueue& recaptureQueue = lumenScene->getSurfaceCacheRecaptureQueue();
        const uint32_t cardCount = static_cast<uint32_t>(mCardResolutions.size());
        for (uint32_t cardIndex = 0; cardIndex < cardCount; ++cardIndex)
        {
            if (mCardResolutions[cardIndex] != 0)
                recaptureQueue.removeCard(mCardRecordBases[cardIndex], getCardPageCount(mCardResolutions[cardIndex]));
            surfaceCacheAtlas.freeCard(mCardRangeSlotBase, cardIndex);
        }
        surfaceCacheAtlas.freeCardPageTableSlots(mCardRangeSlotBase, cardCount * 3u);

        mHasCardRanges = false;
        mCardResolutions.clear();
        mCardRecordBases.clear();
        mCardLightingStates.clear();

        // 这个实例的 mesh 实例表条目现在指向的 cardRangeSlotBase 已经失效——不写哨兵值
        // 的话, GDF 命中反查阶段会拿着这个陈旧下标去 SurfaceCacheAtlas 共享表里读到别的实例
        // (复用了这个槽位的)的 card 数据, 见 kGlobalDistanceFieldInvalidCardRangeSlotBase
        // 的注释
        sceneProxy.getMeshInstanceData(mMeshInstanceIndex).cardRangeSlotBase = kGlobalDistanceFieldInvalidCardRangeSlotBase;
        sceneProxy.markMeshInstanceDataUpdated(mMeshInstanceIndex);
    }

    void MeshRenderProxy::allocateSkinning(uint32_t boneCount)
    {
        assert(!mSkinning && "重复分配前必须先 freeSkinning()");
        if (boneCount == 0)
            return;

        SceneProxy& sceneProxy = getSceneProxy();
        std::optional<SkinningManager::Allocation> allocation =
            sceneProxy.getOrCreateSkinningManager().allocate(boneCount, mMeshInstanceIndex);
        if (!allocation)
        {
            LOG_WARN("SkinningManager matrix table 容量耗尽, 实例 {} 暂时不蒙皮", getInstanceIndex());
            return;
        }

        mSkinning = allocation->handle;

        MeshInstanceData& meshInstanceData = sceneProxy.getMeshInstanceData(mMeshInstanceIndex);
        meshInstanceData.boneMatrixRangeBase = allocation->base;
        meshInstanceData.prevBoneMatrixRangeBase = allocation->base;
        sceneProxy.markMeshInstanceDataUpdated(mMeshInstanceIndex);
    }

    void MeshRenderProxy::freeSkinning() noexcept
    {
        if (!mSkinning)
            return;

        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.getSkinningManager()->free(*mSkinning);
        mSkinning.reset();

        MeshInstanceData& meshInstanceData = sceneProxy.getMeshInstanceData(mMeshInstanceIndex);
        meshInstanceData.boneMatrixRangeBase = kInvalidBoneMatrixRangeBase;
        meshInstanceData.prevBoneMatrixRangeBase = kInvalidBoneMatrixRangeBase;
        sceneProxy.markMeshInstanceDataUpdated(mMeshInstanceIndex);
    }

    void MeshRenderProxy::markSkinningDiscontinuous() noexcept
    {
        if (!mSkinning)
            return;

        getSceneProxy().getSkinningManager()->markDiscontinuous(*mSkinning);
    }

    void MeshRenderProxy::updateSkinningMatrices(std::vector<float3x4>&& matrices)
    {
        if (!mSkinning)
            return;

        getSceneProxy().getSkinningManager()->update(*mSkinning, std::move(matrices));
    }

    void MeshRenderProxy::rebuildSections()
    {
        freeSections();

        SceneProxy& sceneProxy = getSceneProxy();
        std::span<const DrawItem> drawItems = getDrawItemsLOD0();
        if (drawItems.empty())
            return;

        TRangeTable<MeshSectionEntry>& sectionTable = sceneProxy.getSectionTable();
        std::optional<uint32_t> base = sectionTable.allocate(static_cast<uint32_t>(drawItems.size()));
        if (!base)
        {
            LOG_WARN("Section table 容量耗尽, 实例 {} 暂时没有 section 数据", getInstanceIndex());
            return;
        }

        std::vector<MeshSectionEntry> entries;
        entries.reserve(drawItems.size());
        for (const DrawItem& drawItem : drawItems)
            entries.push_back({ drawItem.getGeometryId(), drawItem.material ? drawItem.material->getId() : 0u });
        sectionTable.queueUpload(*base, std::move(entries));

        mSectionTableBase = *base;
        mSectionCount = static_cast<uint32_t>(drawItems.size());
        mHasSections = true;

        sceneProxy.getMeshInstanceData(mMeshInstanceIndex).sectionTableBase = mSectionTableBase;
        sceneProxy.markMeshInstanceDataUpdated(mMeshInstanceIndex);
    }

    void MeshRenderProxy::freeSections() noexcept
    {
        if (!mHasSections)
            return;

        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.getSectionTable().free(mSectionTableBase, mSectionCount);
        mSectionTableBase = 0;
        mSectionCount = 0;
        mHasSections = false;

        sceneProxy.getMeshInstanceData(mMeshInstanceIndex).sectionTableBase = kInvalidSectionTableBase;
        sceneProxy.markMeshInstanceDataUpdated(mMeshInstanceIndex);
    }

    void MeshRenderProxy::setBoundingBox(Boxd boundingBox) noexcept
    {
        DrawableProxy::setBoundingBox(boundingBox);

        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.markMeshBoundsDirty(getMobility());
        sceneProxy.markGlobalDistanceFieldDirty(getBoundingBox(), getMobility());
    }

    void MeshRenderProxy::setMobility(Mobility mobility) noexcept
    {
        Mobility oldMobility = getMobility();
        if (oldMobility == mobility)
            return;

        DrawableProxy::setMobility(mobility);

        // 物体从一个分组挪到另一个分组, 两边包围盒都可能变, 都要标脏
        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.markMeshBoundsDirty(oldMobility);
        sceneProxy.markMeshBoundsDirty(mobility);
        sceneProxy.markGlobalDistanceFieldDirty(getBoundingBox(), oldMobility);
        sceneProxy.markGlobalDistanceFieldDirty(getBoundingBox(), mobility);
    }

}
