#pragma once
#include "SurfaceCacheAtlas.h"
#include "SurfaceCacheRecaptureQueue.h"
#include "SceneProxyData.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/RenderBaseDefines.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>

#include <span>
#include <unordered_map>
#include <vector>

namespace hyp
{
    class MeshRenderProxy;
    class CameraProxy;
    class DirectionalLightProxy;

    // 本帧要做光照更新的一个 card page
    struct SurfaceCacheLightingPage
    {
        MeshRenderProxy* proxy;
        uint32_t cardIndex;
        uint32_t pageIndexX;
        uint32_t pageIndexY;
        uint32_t pageCountX;
        uint32_t pageCountY;
        uint32_t packedAddress;
        uint32_t pageIndirectionOffset;
        int2 extent;
        uint32_t indirectTemporalIndex; // 只在 Indirect 集里有意义
    };

    // 每帧各光照 pass 要处理的页: Direct 集给 CardDirectLighting, Indirect 集给 Radiosity 的四个 pass,
    // Combine 集给 CombineFinalLighting
    struct SurfaceCacheLightingSchedule
    {
        std::vector<SurfaceCacheLightingPage> direct;
        std::vector<SurfaceCacheLightingPage> indirect;
        std::vector<SurfaceCacheLightingPage> combine;
    };

    // 生命周期跟随 SceneProxy, 创建后不会在使用量归零时提前销毁。
    class LumenScene
    {
    public:
        // 某盏方向光这一帧的 Direct Lighting MDF 阴影候选——按光源空间(right/up)铺成固定
        // tileCountX*tileCountY 网格, 每格记录落在这一格里的候选在 tileCandidatePayload 里的
        // [offset, offset+count) 区间, payload 存 SceneProxy::getMeshInstanceData() 那张表的
        // 下标(MeshRenderProxy::getMeshInstanceIndex(), 不是通用的 getInstanceIndex())——
        // 要拿到候选的 SparseDistanceFieldObject(boundsMin/Max/resolution/间接索引表偏移)
        // 必须先查 MeshInstanceData 换出 meshShapeDataOffset, 通用 instanceIndex 查不到这个
        // 偏移。origin/cellSize 是光源空间(right, up)
        // 二维坐标, 不是世界坐标, 换算方式见 updateDirectLightingShadowCandidates() 的注释。
        // 两者都已经相对 referenceOrigin 取过差(见该函数注释), 数值量级安全, 可以放心转 float
        // 传 GPU, 不需要再走 WorldCoord 那套 tile+offset 表示
        struct DirectLightingShadowTileGrid
        {
            double2 origin{};
            double cellSize = 0.0;
            uint32_t tileCountX = 0;
            uint32_t tileCountY = 0;
            std::vector<uint32_t> tileCandidateCounts;
            std::vector<uint32_t> tileCandidateOffsets;
            std::vector<uint32_t> tileCandidatePayload;
            // 这份数据在 LumenScene::mDirectLightingShadowTileGridBuffer 共享 buffer 里的起始
            // 下标(uint32 个数), updateDirectLightingShadowCandidates() 打包上传时填, 跟
            // LightData::shadowTileGridOffset 是同一个值——SceneProxy::update() 负责把它写回
            // 对应方向光的 LightData
            uint32_t gpuBufferOffset = kInvalidSurfaceCacheAtlasPageIndex;
        };

        LumenScene(RenderBackend& renderBackend);

        ~LumenScene();

        SurfaceCacheAtlas& getSurfaceCacheAtlas() noexcept
        {
            return mSurfaceCacheAtlas;
        }

        SurfaceCacheRecaptureQueue& getSurfaceCacheRecaptureQueue() noexcept
        {
            return mSurfaceCacheRecaptureQueue;
        }

        // 每帧在 CardCapturePass 之后调用一次, 决定本帧各光照 pass 要处理的页
        void updateLightingSchedule(std::span<MeshRenderProxy* const> meshRenderProxies);

        const SurfaceCacheLightingSchedule& getLightingSchedule() const noexcept
        {
            return mLightingSchedule;
        }

        // 每帧递增一次, 回绕时跳过 0
        uint32_t getSurfaceCacheUpdateFrameIndex() const noexcept
        {
            return mSurfaceCacheUpdateFrameIndex;
        }

        void advanceSurfaceCacheUpdateFrameIndex() noexcept
        {
            ++mSurfaceCacheUpdateFrameIndex;
            if (mSurfaceCacheUpdateFrameIndex == 0)
                ++mSurfaceCacheUpdateFrameIndex;
        }

        DistanceFieldBrickAtlas& getGlobalDistanceFieldBrickAtlas() noexcept
        {
            return mGlobalDistanceFieldBrickAtlas;
        }

        rhi::Buffer* getDirectLightingShadowTileGridBuffer() const noexcept
        {
            return mDirectLightingShadowTileGridBuffer.get();
        }

        // updateDirectLightingShadowCandidates() 这次调用实际用的 referenceOrigin——
        // CardDirectLightingPass 必须读这份存下来的值, 不能自己重新调用一次
        // cameraProxy->getWorldPosition(), 否则两次独立读数没有任何机制保证一致(每帧只要
        // 有任何东西在这两次读数之间改了相机位置就会错位), 而 GPU 端投影用的 referenceOrigin
        // 必须跟 CPU 建 grid 时用的完全是同一个值, 差一点点整个 tile 坐标系就对不上
        const WorldCoord& getDirectLightingReferenceOrigin() const noexcept
        {
            return mDirectLightingReferenceOrigin;
        }

        // 某盏方向光本帧没有任何候选(没有活跃相机, 或场景里没有 SDF 数据可用)时返回 nullptr
        const DirectLightingShadowTileGrid* getDirectLightingShadowTileGrid(const DirectionalLightProxy* directionalLightProxy) const noexcept;

        // Direct Lighting MDF 阴影候选剔选, 分两阶段: Stage 1(粗筛, 每帧算一次, 所有方向光
        // 共用——一个 mesh 值不值得参与阴影计算只取决于它离活跃相机够不够近, 跟问的是哪盏光
        // 无关)用每个相机的 GDF 最外层 cascade 包围盒(现成状态, 不新开销)的并集筛一遍
        // meshRenderProxies; Stage 2(细分, 每盏方向光各一次)把 Stage 1 存活者投影到这盏光
        // 自己的光源空间, 铺进 tile 网格。referenceOrigin(主相机世界位置, 跟 CardDirectLighting
        // 用的是同一个)用来把每个候选的世界坐标先减成相对坐标、再做光源空间投影——如果直接
        // 拿绝对世界坐标点积, 结果量级跟绝对坐标本身一样大, 大世界场景下 origin/cellSize 转
        // float32 传 GPU 会丢精度; 减掉一个跟场景内容同量级近的参考点之后, 结果量级只跟"场景
        // 内容跨度"挂钩, 转 float 是安全的。这一步跑完顺带把结果打包上传共享 buffer(见
        // prepareDirectLightingShadowTileGridUpload()), 调用方还要把每盏方向光算出来的
        // DirectLightingShadowTileGrid::gpuBufferOffset 写回它的 LightData
        void updateDirectLightingShadowCandidates(
            std::span<MeshRenderProxy* const> meshRenderProxies,
            std::span<CameraProxy* const> cameraProxies,
            std::span<DirectionalLightProxy* const> directionalLightProxies,
            const WorldCoord& referenceOrigin);

        bool hasPendingDirectLightingShadowTileGridUpload() const noexcept
        {
            return mDirectLightingShadowTileGridDirty;
        }

        // 每帧调用一次(在 updateDirectLightingShadowCandidates() 之后, 需要读这块数据的
        // pass 的 execute 阶段里)——这块数据是每帧从头整个重建, 不是增量的, 所以只有一次
        // 全量 flush, 不像 CardPageTable 那样要跟踪多笔独立的 pending upload
        void prepareDirectLightingShadowTileGridUpload(rhi::CommandList& cmdList);

    private:
        RenderBackend& mRenderBackend;
        SurfaceCacheAtlas mSurfaceCacheAtlas;
        SurfaceCacheRecaptureQueue mSurfaceCacheRecaptureQueue;
        SurfaceCacheLightingSchedule mLightingSchedule;
        uint32_t mSurfaceCacheUpdateFrameIndex = 1;
        DistanceFieldBrickAtlas mGlobalDistanceFieldBrickAtlas;
        std::unordered_map<const DirectionalLightProxy*, DirectLightingShadowTileGrid> mDirectLightingShadowTileGrids;
        WorldCoord mDirectLightingReferenceOrigin;

        // 固定容量, 每帧整个重建内容(不是持久分配), 见 kDirectLightingShadowTileGridCapacity
        // 的注释——超出容量的部分会被丢弃并打警告日志, 不做动态扩容
        TRef<rhi::Buffer> mDirectLightingShadowTileGridBuffer;
        std::vector<uint32_t> mDirectLightingShadowTileGridData;
        uint32_t mDirectLightingShadowTileGridUsedCount = 0;
        bool mDirectLightingShadowTileGridDirty = false;
        bool mDirectLightingShadowTileGridBufferInitialized = false;
    };
}
