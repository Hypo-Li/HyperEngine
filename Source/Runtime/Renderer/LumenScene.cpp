#include "LumenScene.h"

#include "CameraProxy.h"
#include "DirectionalLightProxy.h"
#include "MeshRenderProxy.h"

#include <RenderBase/MeshShapeProxy.h>
#include <Core/Common/LogSystem.h>
#include <Core/Math/Box.h>
#include <Core/Math/Math.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace hyp
{
    namespace
    {
        // 网格分辨率固定, 不随候选实际跨度自适应——跨度越大, 网格越粗只是让 Stage 2 少省一点
        // 剔选精度, 不影响正确性
        constexpr uint32_t kDirectLightingShadowTileGridResolution = 16;

        // GDF brick 容量——UE Lumen 按 pagesPerAxis³ × cascade 数 × occupancyRatio(默认 0.3)
        // 算生产规模的预算, 代入我们的 36³×4×0.3 约等于 56000; 当前测试场景规模远用不到这个
        // 量级, 起始按跟 per-mesh 的 kSparseBrickAtlasCapacity 同一量级的 8000 预留, 顶到上限
        // 再用同一个公式重新算
        constexpr uint32_t kGlobalDistanceFieldBrickAtlasCapacity = 8000;

        // 每盏方向光的 tile grid 打包数据(header + offsets + payload)共享这一份 buffer 的
        // 总容量——每帧整个重建、不做增量分配, 超出的部分丢弃并打警告。256KiB 按当前测试
        // 场景规模(个位数方向光、每盏 16x16 tile、候选数百量级)有充足余量, 顶到上限再考虑
        // 是否需要动态扩容
        constexpr uint32_t kDirectLightingShadowTileGridCapacity = 65536;

        constexpr int32_t kDirectLightingUpdateFactor = 32; // 对应 UE r.LumenScene.DirectLighting.UpdateFactor
        constexpr int32_t kRadiosityUpdateFactor = 64;      // 对应 UE r.LumenScene.Radiosity.UpdateFactor
        constexpr uint32_t kLightingPriorityBucketCount = 16; // 对应 UE PRIORITY_HISTOGRAM_SIZE
        constexpr float kLightingPriorityDistanceScale = 25.0f; // 米, 对应 UE 第一级 GDF clipmap 的范围

        enum LightingContext : uint32_t
        {
            Lighting_Context_Direct = 0,
            Lighting_Context_Indirect = 1,
            Lighting_Context_Count = 2
        };

        struct LightingCandidate
        {
            MeshRenderProxy* proxy;
            uint32_t cardIndex;
            uint32_t tiles;
            uint32_t bucket[Lighting_Context_Count];
            bool selected[Lighting_Context_Count];
        };

        // 对应 UE SetLightingUpdateAtlasSize: 每帧最多更新的 tile 数, 至少够更新一整页
        uint32_t computeLightingUpdateTileBudget(int32_t updateFactor)
        {
            const float multPerComponent = 1.0f / std::sqrt(static_cast<float>(std::clamp(updateFactor, 1, 1024)));
            int32_t edge = static_cast<int32_t>(kSurfaceCacheAtlasResolution * multPerComponent + 0.5f);
            edge = (edge + kSurfaceCacheCardTileSize - 1) / kSurfaceCacheCardTileSize * kSurfaceCacheCardTileSize;
            edge = std::max(edge, kSurfaceCachePageSize);
            const uint32_t tilesPerAxis = static_cast<uint32_t>(edge / kSurfaceCacheCardTileSize);
            return tilesPerAxis * tilesPerAxis;
        }

        // 对应 UE GetPriorityBucketIndex: 越久没更新、离相机越近, 桶下标越小
        uint32_t computeLightingPriorityBucket(uint32_t frame, uint32_t lastUpdateFrame, float viewerDistance)
        {
            const uint32_t framesSinceLastUpdate = lastUpdateFrame == 0 ? 2048u : frame - lastUpdateFrame;
            const float updateSpeed = 1.0f / (1.0f + std::max(viewerDistance / kLightingPriorityDistanceScale, 0.0f));
            const float priority = std::max(4.0f * static_cast<float>(framesSinceLastUpdate) * updateSpeed, 1.0f);
            const uint32_t level = std::min(static_cast<uint32_t>(std::log2(priority)), kLightingPriorityBucketCount - 1);
            return kLightingPriorityBucketCount - 1 - level;
        }

        // 对应 UE SelectMaxUpdateBucket + BuildCardsUpdateList: 取优先级最高的若干桶直到用完 tile 预算,
        // 最后一个桶只取放得下的部分; 第一张入选的卡不受预算限制, 保证比预算更大的卡也能轮到
        void selectLightingCandidates(std::vector<LightingCandidate>& candidates, LightingContext context, uint32_t maxTiles)
        {
            uint32_t histogram[kLightingPriorityBucketCount] = {};
            for (const LightingCandidate& candidate : candidates)
                histogram[candidate.bucket[context]] += candidate.tiles;

            uint32_t maxBucket = kLightingPriorityBucketCount;
            uint32_t maxBucketTiles = maxTiles;
            uint32_t tilesBeforeBucket = 0;
            for (uint32_t bucket = 0; bucket < kLightingPriorityBucketCount; ++bucket)
            {
                if (tilesBeforeBucket + histogram[bucket] >= maxTiles)
                {
                    maxBucket = bucket;
                    maxBucketTiles = maxTiles - tilesBeforeBucket;
                    break;
                }
                tilesBeforeBucket += histogram[bucket];
            }

            uint32_t maxBucketAllocated = 0;
            uint32_t totalAllocated = 0;
            bool anySelected = false;
            for (LightingCandidate& candidate : candidates)
            {
                const uint32_t bucket = candidate.bucket[context];
                bool select = bucket <= maxBucket;
                if (select && anySelected)
                {
                    if (bucket == maxBucket && maxBucketAllocated + candidate.tiles > maxBucketTiles)
                        select = false;
                    else if (totalAllocated + candidate.tiles > maxTiles)
                        select = false;
                }

                candidate.selected[context] = select;
                if (select)
                {
                    anySelected = true;
                    totalAllocated += candidate.tiles;
                    if (bucket == maxBucket)
                        maxBucketAllocated += candidate.tiles;
                }
            }
        }
    }

    LumenScene::LumenScene(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend),
        mSurfaceCacheAtlas(renderBackend),
        // enableStorageWrite=true: GlobalDistanceFieldComposite 的 compute shader 直接
        // WTexture3D::Store() 写这张 atlas, 不是走 queueBrickUploads() 的 CPU->GPU 拷贝
        mGlobalDistanceFieldBrickAtlas(renderBackend, kGlobalDistanceFieldBrickAtlasCapacity, true)
    {
        mDirectLightingShadowTileGridData.resize(kDirectLightingShadowTileGridCapacity, 0u);

        rhi::BufferDesc bufferDesc;
        bufferDesc.size = static_cast<uint64_t>(kDirectLightingShadowTileGridCapacity) * sizeof(uint32_t);
        bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
        mDirectLightingShadowTileGridBuffer = mRenderBackend.getDevice().createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
        mDirectLightingShadowTileGridBuffer->setDebugName("LumenScene.DirectLightingShadowTileGrid");
    }

    LumenScene::~LumenScene()
    {
        // 跟 SurfaceCacheAtlas 析构同一个理由: 场景可能在运行中途销毁, 这时候 GPU 上可能还有
        // 飞行中的渲染指令在读这块 buffer, 延迟到 kMaxFramesInFlight 帧之后再真正销毁
        mRenderBackend.getDeferredTaskQueue().push(
            [buffer = std::move(mDirectLightingShadowTileGridBuffer)]() mutable
            {
                buffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void LumenScene::updateLightingSchedule(std::span<MeshRenderProxy* const> meshRenderProxies)
    {
        mLightingSchedule.direct.clear();
        mLightingSchedule.indirect.clear();
        mLightingSchedule.combine.clear();

        const uint32_t frame = mSurfaceCacheUpdateFrameIndex;

        std::vector<LightingCandidate> candidates;
        for (MeshRenderProxy* proxy : meshRenderProxies)
        {
            MeshShapeProxy* meshShapeProxy = proxy->getMeshShapeProxy();
            if (!meshShapeProxy || !proxy->hasCardRanges())
                continue;

            const uint32_t cardCount = static_cast<uint32_t>(meshShapeProxy->getCards().size());
            for (uint32_t cardIndex = 0; cardIndex < cardCount; ++cardIndex)
            {
                const CardPageRange range = mSurfaceCacheAtlas.getCardPageRange(proxy->getCardRangeSlotBase(), cardIndex);
                if (range.pageCount.x == 0 || range.pageCount.y == 0)
                    continue;

                // 多页卡的页都是整页, 单页卡取它实际的尺寸
                const uint32_t pageCount = static_cast<uint32_t>(range.pageCount.x * range.pageCount.y);
                uint32_t texels = pageCount * kSurfaceCachePageSize * kSurfaceCachePageSize;
                if (pageCount == 1)
                {
                    const int2 extent = mSurfaceCacheAtlas.getCardPageRegion(proxy->getCardRangeSlotBase(), cardIndex, 0).pixelExtent;
                    texels = static_cast<uint32_t>(extent.x * extent.y);
                }

                const SurfaceCacheCardLightingState& state = proxy->getCardLightingState(cardIndex);
                LightingCandidate candidate;
                candidate.proxy = proxy;
                candidate.cardIndex = cardIndex;
                candidate.tiles = texels / (kSurfaceCacheCardTileSize * kSurfaceCacheCardTileSize);
                candidate.bucket[Lighting_Context_Direct] = computeLightingPriorityBucket(frame, state.lastDirectUpdateFrame, state.viewerDistance);
                candidate.bucket[Lighting_Context_Indirect] = computeLightingPriorityBucket(frame, state.lastIndirectUpdateFrame, state.viewerDistance);
                candidate.selected[Lighting_Context_Direct] = false;
                candidate.selected[Lighting_Context_Indirect] = false;
                candidates.push_back(candidate);
            }
        }

        selectLightingCandidates(candidates, Lighting_Context_Direct, computeLightingUpdateTileBudget(kDirectLightingUpdateFactor));
        selectLightingCandidates(candidates, Lighting_Context_Indirect, computeLightingUpdateTileBudget(kRadiosityUpdateFactor));

        for (const LightingCandidate& candidate : candidates)
        {
            const bool updateDirect = candidate.selected[Lighting_Context_Direct];
            const bool updateIndirect = candidate.selected[Lighting_Context_Indirect];
            if (!updateDirect && !updateIndirect)
                continue;

            MeshRenderProxy* proxy = candidate.proxy;
            SurfaceCacheCardLightingState& state = proxy->getCardLightingState(candidate.cardIndex);
            const CardPageRange range = mSurfaceCacheAtlas.getCardPageRange(proxy->getCardRangeSlotBase(), candidate.cardIndex);
            const uint32_t pageCountX = static_cast<uint32_t>(range.pageCount.x);
            const uint32_t pageCountY = static_cast<uint32_t>(range.pageCount.y);

            for (uint32_t pageIndexY = 0; pageIndexY < pageCountY; ++pageIndexY)
            {
                for (uint32_t pageIndexX = 0; pageIndexX < pageCountX; ++pageIndexX)
                {
                    const uint32_t pageLinearIndex = pageIndexY * pageCountX + pageIndexX;

                    SurfaceCacheLightingPage page;
                    page.proxy = proxy;
                    page.cardIndex = candidate.cardIndex;
                    page.pageIndexX = pageIndexX;
                    page.pageIndexY = pageIndexY;
                    page.pageCountX = pageCountX;
                    page.pageCountY = pageCountY;
                    page.packedAddress = mSurfaceCacheAtlas.getCardPageTable()[range.pageIndirectionOffset + pageLinearIndex];
                    page.pageIndirectionOffset = range.pageIndirectionOffset;
                    page.extent = mSurfaceCacheAtlas.getCardPageRegion(proxy->getCardRangeSlotBase(), candidate.cardIndex, pageLinearIndex).pixelExtent;
                    page.indirectTemporalIndex = state.indirectTemporalIndex;

                    if (updateDirect)
                        mLightingSchedule.direct.push_back(page);
                    if (updateIndirect)
                        mLightingSchedule.indirect.push_back(page);
                    mLightingSchedule.combine.push_back(page);
                }
            }

            if (updateDirect)
                state.lastDirectUpdateFrame = frame;
            if (updateIndirect)
            {
                state.lastIndirectUpdateFrame = frame;
                ++state.indirectTemporalIndex;
            }
        }
    }

    const LumenScene::DirectLightingShadowTileGrid* LumenScene::getDirectLightingShadowTileGrid(const DirectionalLightProxy* directionalLightProxy) const noexcept
    {
        auto it = mDirectLightingShadowTileGrids.find(directionalLightProxy);
        return it != mDirectLightingShadowTileGrids.end() ? &it->second : nullptr;
    }

    void LumenScene::updateDirectLightingShadowCandidates(
        std::span<MeshRenderProxy* const> meshRenderProxies,
        std::span<CameraProxy* const> cameraProxies,
        std::span<DirectionalLightProxy* const> directionalLightProxies,
        const WorldCoord& referenceOrigin)
    {
        mDirectLightingShadowTileGrids.clear();
        mDirectLightingShadowTileGridUsedCount = 0;
        // 记下这次实际用的 referenceOrigin——CardDirectLightingPass 必须读这份值(见
        // getDirectLightingReferenceOrigin() 的注释), 不能自己重新取一次相机位置。即使这一帧
        // 没有方向光/相机、下面提前 return, 也要先存, 不留上一帧的陈旧值被误用
        mDirectLightingReferenceOrigin = referenceOrigin;
        if (directionalLightProxies.empty() || cameraProxies.empty())
            return;

        const double3 referenceOriginD = referenceOrigin.toDouble3();

        // 不能用 cameraProxy->getGlobalDistanceField().getCascades()[...].center——这个值只在
        // GlobalDistanceFieldCompositePass::addToRenderGraph() 里的 gdf.update() 才会重新对齐
        // 到本帧相机位置, 而那次调用发生在 DeferredPipeline::addToRenderGraph() 阶段, 晚于
        // 这个函数(挂在 SceneRenderer::render() 的 "Upload" Pass 构建阶段, 见
        // SceneProxy::update() 调用处的注释)。这里读到的 center 是上一帧遗留的旧值, 相机
        // 刚发生大幅移动(比如挪到 mesh 另一侧)时这份陈旧包围盒可能完全盖不到目标 mesh,
        // Stage 1 会把本该参与的候选整体误判成"不相关", 表现为影子突然全部消失——用
        // GDF_Visualizer_Cascade_Desync_Bug_RootCause.md 记录的是同一类时序错位, 只是这次
        // 出在新加的 Stage 1 相关性判断上。halfExtent 是构造时定死的常量(不随相机变), 只有
        // center 才会失配, 直接绕开对它的依赖: 用相机当前世界坐标(读的时候必然是最新值)
        // 现算一个同样以相机为中心、半宽等于最外层 cascade halfExtent 的包围盒, 语义上
        // 跟"最外层 cascade 应该在的位置"完全一致, 又不需要等 gdf.update() 先跑完
        std::vector<Boxd> cameraRelevanceBounds;
        cameraRelevanceBounds.reserve(cameraProxies.size());
        for (CameraProxy* cameraProxy : cameraProxies)
        {
            const float outermostHalfExtent =
                cameraProxy->getGlobalDistanceField().getCascades()[kGlobalDistanceFieldCascadeCount - 1].halfExtent;
            const double3 cameraWorldPos = cameraProxy->getWorldPosition();
            cameraRelevanceBounds.emplace_back(
                cameraWorldPos - double3(outermostHalfExtent),
                cameraWorldPos + double3(outermostHalfExtent));
        }

        // Stage 1: 粗筛, 每帧一次, 所有方向光共用——是否值得参与阴影计算只取决于离活跃相机
        // 够不够近, 跟问的是哪盏光无关。过滤条件照抄 GlobalDistanceField::updateCascadeLayer():
        // 没有距离场数据(蒙皮网格等)的实例天然被 hasDistanceFieldData() 挡在外面, mobility 不参与筛选
        std::vector<MeshRenderProxy*> stage1Candidates;
        for (MeshRenderProxy* meshRenderProxy : meshRenderProxies)
        {
            MeshShapeProxy* meshShapeProxy = meshRenderProxy->getMeshShapeProxy();
            if (!meshShapeProxy || !meshShapeProxy->hasDistanceFieldData())
                continue;

            const Boxd& worldBounds = meshRenderProxy->getBoundingBox();
            bool isRelevant = std::any_of(
                cameraRelevanceBounds.begin(), cameraRelevanceBounds.end(),
                [&worldBounds](const Boxd& cameraBounds) { return cameraBounds.intersect(worldBounds); });
            if (isRelevant)
                stage1Candidates.push_back(meshRenderProxy);
        }

        if (stage1Candidates.empty())
            return;

        // Stage 2: 细分, 每盏方向光各一次。把 Stage 1 存活者的世界包围盒投影到这盏光自己的
        // 光源空间(right/up 轴), 用"包围盒中心在轴上的投影 ± 各世界轴分量在该轴上的投影
        // 之和"这个标准做法算出光源空间下的 2D 包围矩形, 再铺进这盏光专属的 tile 网格。
        // 中心先减掉 referenceOriginD 转成相对坐标再投影(而不是直接拿绝对世界坐标点积)——
        // 见 updateDirectLightingShadowCandidates() 声明处的注释, 这一步是为了让 origin/
        // cellSize 转 float32 传 GPU 时量级安全
        for (DirectionalLightProxy* directionalLightProxy : directionalLightProxies)
        {
            if (!directionalLightProxy->isVisible())
                continue;

            double3 lightForward, lightRight, lightUp;
            DirectionalLightProxy::computeLightSpaceBasis(directionalLightProxy->getDirection(), lightForward, lightRight, lightUp);

            struct ProjectedCandidate
            {
                uint32_t meshInstanceIndex;
                double2 min;
                double2 max;
            };
            std::vector<ProjectedCandidate> projectedCandidates;
            projectedCandidates.reserve(stage1Candidates.size());

            double2 gridMin(std::numeric_limits<double>::max());
            double2 gridMax(std::numeric_limits<double>::lowest());
            for (MeshRenderProxy* meshRenderProxy : stage1Candidates)
            {
                const Boxd& worldBounds = meshRenderProxy->getBoundingBox();
                double3 center = worldBounds.getCenter() - referenceOriginD;
                double3 extent = worldBounds.getExtent();
                double centerRight = Math::dot(center, lightRight);
                double centerUp = Math::dot(center, lightUp);
                double radiusRight = Math::dot(extent, glm::abs(lightRight));
                double radiusUp = Math::dot(extent, glm::abs(lightUp));

                ProjectedCandidate projectedCandidate;
                projectedCandidate.meshInstanceIndex = meshRenderProxy->getMeshInstanceIndex();
                projectedCandidate.min = double2(centerRight - radiusRight, centerUp - radiusUp);
                projectedCandidate.max = double2(centerRight + radiusRight, centerUp + radiusUp);
                projectedCandidates.push_back(projectedCandidate);

                gridMin = glm::min(gridMin, projectedCandidate.min);
                gridMax = glm::max(gridMax, projectedCandidate.max);
            }

            DirectLightingShadowTileGrid& tileGrid = mDirectLightingShadowTileGrids[directionalLightProxy];
            tileGrid.origin = gridMin;
            tileGrid.tileCountX = kDirectLightingShadowTileGridResolution;
            tileGrid.tileCountY = kDirectLightingShadowTileGridResolution;
            double2 gridSize = gridMax - gridMin;
            tileGrid.cellSize = std::max({ gridSize.x, gridSize.y, 1e-4 }) / kDirectLightingShadowTileGridResolution;

            const uint32_t tileCount = tileGrid.tileCountX * tileGrid.tileCountY;
            std::vector<std::vector<uint32_t>> perTileInstanceIndices(tileCount);
            for (const ProjectedCandidate& projectedCandidate : projectedCandidates)
            {
                auto toTileIndex = [&](double coordinate, double origin, uint32_t tileCountOnAxis) {
                    int32_t tileIndex = static_cast<int32_t>(std::floor((coordinate - origin) / tileGrid.cellSize));
                    return std::clamp(tileIndex, 0, static_cast<int32_t>(tileCountOnAxis) - 1);
                };
                int32_t tileMinX = toTileIndex(projectedCandidate.min.x, tileGrid.origin.x, tileGrid.tileCountX);
                int32_t tileMaxX = toTileIndex(projectedCandidate.max.x, tileGrid.origin.x, tileGrid.tileCountX);
                int32_t tileMinY = toTileIndex(projectedCandidate.min.y, tileGrid.origin.y, tileGrid.tileCountY);
                int32_t tileMaxY = toTileIndex(projectedCandidate.max.y, tileGrid.origin.y, tileGrid.tileCountY);

                for (int32_t y = tileMinY; y <= tileMaxY; ++y)
                {
                    for (int32_t x = tileMinX; x <= tileMaxX; ++x)
                    {
                        perTileInstanceIndices[y * tileGrid.tileCountX + x].push_back(projectedCandidate.meshInstanceIndex);
                    }
                }
            }

            tileGrid.tileCandidateCounts.resize(tileCount);
            tileGrid.tileCandidateOffsets.resize(tileCount);
            tileGrid.tileCandidatePayload.clear();
            uint32_t runningOffset = 0;
            for (uint32_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
            {
                tileGrid.tileCandidateCounts[tileIndex] = static_cast<uint32_t>(perTileInstanceIndices[tileIndex].size());
                tileGrid.tileCandidateOffsets[tileIndex] = runningOffset;
                runningOffset += tileGrid.tileCandidateCounts[tileIndex];
            }
            tileGrid.tileCandidatePayload.reserve(runningOffset);
            for (uint32_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
            {
                for (uint32_t meshInstanceIndex : perTileInstanceIndices[tileIndex])
                    tileGrid.tileCandidatePayload.push_back(meshInstanceIndex);
            }

            // 打包进共享 buffer: header(originX, originY, cellSize, tileCountX, tileCountY) +
            // tileCandidateOffsets(带一个末尾哨兵 = 总候选数, 省掉单独传 tileCandidateCounts) +
            // tileCandidatePayload。CardDirectLighting.slang 按同样的偏移量手动解包, 见该文件
            // 顶部注释
            const uint32_t headerWordCount = 5;
            const uint32_t neededWordCount = headerWordCount + (tileCount + 1) + runningOffset;
            if (mDirectLightingShadowTileGridUsedCount + neededWordCount > kDirectLightingShadowTileGridCapacity)
            {
                LOG_WARN("LumenScene::updateDirectLightingShadowCandidates: 共享 buffer 容量不够(已用 {}, 还需要 {}, 容量 {}), 这盏方向光这一帧没有 MDF 阴影数据",
                    mDirectLightingShadowTileGridUsedCount, neededWordCount, kDirectLightingShadowTileGridCapacity);
                tileGrid.gpuBufferOffset = kInvalidSurfaceCacheAtlasPageIndex;
                continue;
            }

            const uint32_t base = mDirectLightingShadowTileGridUsedCount;
            tileGrid.gpuBufferOffset = base;

            uint32_t* dst = mDirectLightingShadowTileGridData.data() + base;
            // origin/cellSize 存进共享 buffer 前从 double 转成 float——转换在这里做是安全的,
            // 见 updateDirectLightingShadowCandidates() 声明处关于 referenceOrigin 的注释。
            // std::bit_cast 重新解释成 uint32 是 GlobalDistanceFieldCompositePass.cpp 打包
            // cascadeInfoWords 时已经在用的同一个手法
            dst[0] = std::bit_cast<uint32_t>(static_cast<float>(tileGrid.origin.x));
            dst[1] = std::bit_cast<uint32_t>(static_cast<float>(tileGrid.origin.y));
            dst[2] = std::bit_cast<uint32_t>(static_cast<float>(tileGrid.cellSize));
            dst[3] = tileGrid.tileCountX;
            dst[4] = tileGrid.tileCountY;

            uint32_t* offsetsDst = dst + headerWordCount;
            std::memcpy(offsetsDst, tileGrid.tileCandidateOffsets.data(), tileCount * sizeof(uint32_t));
            offsetsDst[tileCount] = runningOffset; // CSR 哨兵: 总候选数

            uint32_t* payloadDst = offsetsDst + tileCount + 1;
            if (runningOffset > 0)
            {
                std::memcpy(payloadDst, tileGrid.tileCandidatePayload.data(), runningOffset * sizeof(uint32_t));
            }

            mDirectLightingShadowTileGridUsedCount += neededWordCount;
        }

        mDirectLightingShadowTileGridDirty = mDirectLightingShadowTileGridUsedCount > 0;
    }

    void LumenScene::prepareDirectLightingShadowTileGridUpload(rhi::CommandList& cmdList)
    {
        // 每帧整个重建、整个上传(不是增量), 数据量按当前场景规模很小(容量上限 256KiB),
        // 没必要像 SurfaceCacheAtlas::CardPageTable 那样做分帧 staging——一次性通过
        // StagingAllocator 传, 传不下(单帧 staging 预算耗尽)就跳过等下一帧重试, 这一帧
        // 宁可继续用上一帧的旧内容也不要传一半
        if (!mDirectLightingShadowTileGridDirty)
            return;

        const uint64_t byteSize = static_cast<uint64_t>(mDirectLightingShadowTileGridUsedCount) * sizeof(uint32_t);
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();
        auto stagingAllocation = stagingAllocator.tryAllocate(byteSize);
        if (!stagingAllocation)
            return; // 本帧 staging 预算耗尽, 下一帧再试, mDirectLightingShadowTileGridDirty 保持 true

        std::memcpy(stagingAllocation->mappedData, mDirectLightingShadowTileGridData.data(), byteSize);

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mDirectLightingShadowTileGridBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = rhi::kWholeBuffer;
        preBarrier.oldState = mDirectLightingShadowTileGridBufferInitialized ? rhi::ResourceState::Shader_Storage_Read : rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion region;
        region.srcOffset = stagingAllocation->offset;
        region.dstOffset = 0;
        region.size = byteSize;
        cmdList.copyBuffer(stagingAllocation->buffer, mDirectLightingShadowTileGridBuffer.get(), { &region, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mDirectLightingShadowTileGridBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = rhi::kWholeBuffer;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmdList.insertBarriers({ &postBarrier, 1 });

        mDirectLightingShadowTileGridBufferInitialized = true;
        mDirectLightingShadowTileGridDirty = false;
    }
}
