#pragma once
#include "RendererDefines.h"
#include "SceneProxyData.h"
#include "SceneView.h"
#include "CameraProxy.h"
#include "MeshRenderProxy.h"
#include "DirectionalLightProxy.h"
#include "PointLightProxy.h"
#include "SpotLightProxy.h"
#include "AtmosphereProxy.h"
#include "SkyLightProxy.h"
#include "SkyBackgroundProxy.h"
#include "GSplatRenderProxy.h"
#include "GSplatScene.h"
#include "LumenScene.h"
#include "QuadManager.h"
#include "QuadRenderProxy.h"
#include "SkinningManager.h"

#include <Core/Container/DirtySlotPool.h>
#include <RenderBase/RangeTable.h>
#include <RenderBase/RenderProxy.h>
#include <RHI/Buffer.h>
#include <RHI/BindGroup.h>

#include <algorithm>
#include <cassert>
#include <type_traits>

namespace hyp
{
    class SceneProxy : public RenderProxy
    {
    public:
        SceneProxy(uint32_t sceneId, const rhi::Buffer& indicesBuffer);

        ~SceneProxy() noexcept;

        uint32_t getSceneId() const noexcept
        {
            return mSceneId;
        }

        // TranslucentPass/DirectLightingPass 需要在自己专属的 pass bind group 里复用这两个
        // buffer——它们本来就是 SceneProxy 自己拥有并且已经在用的资源, 这里只是多开一个只读
        // 入口, 不代表 SceneProxy 对光照体积纹理这类东西本身有任何感知
        const rhi::Buffer& getInstanceDataBuffer() const noexcept
        {
            return *mInstanceDataBuffer;
        }

        const rhi::Buffer& getLightDataBuffer() const noexcept
        {
            return *mLightDataBuffer;
        }

        const rhi::BindGroup& getInstanceDataBindGroup() const noexcept
        {
            return *mInstanceDataBindGroup;
        }

        const rhi::BindGroup& getDrawBindGroup() const noexcept
        {
            return *mDrawBindGroup;
        }

        const rhi::BindGroup& getSkeletalDrawBindGroup() const noexcept
        {
            assert(mSkeletalDrawBindGroup && "骨骼网格的 draw item 存在, SkinningManager 不应该是空");
            return *mSkeletalDrawBindGroup;
        }

        const rhi::BindGroup& getQuadDrawBindGroup() const noexcept
        {
            assert(mQuadDrawBindGroup && "四边形的 draw item 存在, QuadManager 不应该是空");
            return *mQuadDrawBindGroup;
        }

        const rhi::BindGroup& getDrawBindGroup(VertexLayoutType vertexLayoutType) const noexcept
        {
            switch (vertexLayoutType)
            {
            case VertexLayoutType::Skeletal_Mesh: return getSkeletalDrawBindGroup();
            case VertexLayoutType::Quad: return getQuadDrawBindGroup();
            default: return getDrawBindGroup();
            }
        }

        InstanceData& getInstanceData(uint32_t instanceIndex) noexcept
        {
            return mInstanceData[instanceIndex];
        }

        const InstanceData& getInstanceData(uint32_t instanceIndex) const noexcept
        {
            return mInstanceData[instanceIndex];
        }

        std::span<const InstanceData> getInstanceData() const noexcept
        {
            return mInstanceData.getElements();
        }

        // 当前 instanceIndex 的容量上界(不是"正在使用中的数量"——释放掉的下标可能被复用,
        // 这个只用来给"按 instanceIndex 直接索引的表"决定要建多大, 见 TranslucentPass 的
        // InstanceLightIndices 用法)
        uint32_t getInstanceCount() const noexcept
        {
            return mInstanceData.size();
        }

        void markInstanceDataUpdated(uint32_t instanceIndex) noexcept;

        bool isInstanceTransformUpdated(uint32_t instanceIndex) const noexcept;

        void markInstanceTransformUpdated(uint32_t instanceIndex) noexcept;

        LightData& getLightData(uint32_t lightIndex) noexcept
        {
            return mLightData[lightIndex];
        }

        const LightData& getLightData(uint32_t lightIndex) const noexcept
        {
            return mLightData[lightIndex];
        }

        void markLightDataUpdated(uint32_t lightIndex) noexcept;

        uint32_t getLightCount() const noexcept
        {
            return mLightData.size();
        }

        // 逐 mesh 实例目录表(见 SceneProxyData.h 里 MeshInstanceData 的注释)——条目
        // 本身跟 InstanceData 一样, 由 MeshRenderProxy 在构造/析构时统一申请/归还
        // (不是等到 setMeshShapeProxy() 才临时分配——这张表
        // 不该被理解成"只服务当前这几个字段", 以后要加跟 mesh shape 无关的逐实例字段
        // 照样加在这张表里, 生命周期跟着实例走最自然)。GlobalDistanceField 的 Object Grid
        // 存的就是这个下标(meshInstanceIndex), 不是通用的 instanceIndex
        // 非 const(不是 getInstanceDataBuffer()/getLightDataBuffer() 那种只读入口)——
        // 唯一的消费方(GlobalDistanceFieldVisualizerPass)要把它 importBuffer() 进渲染图,
        // 那个接口要的是 rhi::Buffer*
        rhi::Buffer& getMeshInstanceDataBuffer() const noexcept
        {
            return *mMeshInstanceDataBuffer;
        }

        MeshInstanceData& getMeshInstanceData(uint32_t meshInstanceIndex) noexcept
        {
            return mMeshInstanceData[meshInstanceIndex];
        }

        const MeshInstanceData& getMeshInstanceData(uint32_t meshInstanceIndex) const noexcept
        {
            return mMeshInstanceData[meshInstanceIndex];
        }

        void markMeshInstanceDataUpdated(uint32_t meshInstanceIndex) noexcept;

        std::span<MeshRenderProxy* const> getMeshRenderProxies() const noexcept
        {
            return mMeshRenderProxies;
        }

        // 元素 proxy 在构造/析构里登记和摘除自己
        template <typename T>
        void registerElement(T* element)
        {
            getElementList<T>().push_back(element);
        }

        template <typename T>
        void unregisterElement(T* element)
        {
            auto& elements = getElementList<T>();
            auto it = std::find(elements.begin(), elements.end(), element);
            assert(it != elements.end());
            elements.erase(it);
        }

        std::span<QuadRenderProxy* const> getQuadRenderProxies() const noexcept
        {
            return mQuadRenderProxies;
        }

        // 按 Entity::Mobility 分 Static/Dynamic 两组惰性维护的包围盒并集, 用于(比如)
        // DirectionalLightProxy 算级联阴影视锥。新增物体 O(1) 标脏, 移除/移动物体也只标脏
        // 对应那一组(不做真正的增量维护——AABB 合并没法在不全量重扫的情况下正确处理移除/
        // 缩小, 除非引入 BVH/区间树), 只有真正被 dirty 过的那一组在下次查询时才会重扫。
        // Static 物体加载完场景基本不再动, 这一组几乎总是命中缓存
        Boxd getSceneBounds() const noexcept;

        // MeshRenderProxy::setBoundingBox/setMobility 用这个通知"某个 mobility 分组的包围盒
        // 需要重算"
        void markMeshBoundsDirty(Mobility mobility) const noexcept;

        // MeshRenderProxy 增删/包围盒变化(setBoundingBox/setMobility)时调用: 对场景里每个
        // 相机的每一级 GDF cascade 做包围盒相交测试, 相交的那一级按 mobility 标记对应层
        // (静态/动态)重算。没开 Lumen 直接跳过
        void markGlobalDistanceFieldDirty(Boxd instanceWorldBounds, Mobility mobility) const noexcept;

        std::span<CameraProxy* const> getCameraProxies() const noexcept
        {
            return mCameraProxies;
        }

        std::span<DirectionalLightProxy* const> getDirectionalLightProxies() const noexcept
        {
            return mDirectionalLightProxies;
        }

        // Point/Spot 混在同一个 vector 里(靠 lightData.type 分支)——DirectLightingPass 的双
        // 路径判断、TranslucentPass 的 CSR 剔选都只用到 LocalLightProxy 基类接口(position/
        // attenuationRadius/getLightIndex), 不需要按类型分开存
        std::span<LocalLightProxy* const> getLocalLightProxies() const noexcept
        {
            return mLocalLightProxies;
        }

        std::span<AtmosphereProxy* const> getAtmosphereProxies() const noexcept
        {
            return mAtmosphereProxies;
        }

        AtmosphereProxy* getActiveAtmosphereProxy() const noexcept;

        SkyBackgroundProxy* getActiveSkyBackgroundProxy() const noexcept;

        float3 getMainLightDirection(uint32_t index) const noexcept
        {
            return mMainLightDirections[index];
        }

        float3 getMainLightIntensity(uint32_t index) const noexcept
        {
            return mMainLightIntensities[index];
        }

        std::span<SkyLightProxy* const> getSkyLightProxies() const noexcept
        {
            return mSkyLightProxies;
        }

        SkyLightProxy* getActiveSkyLightProxy() const noexcept;

        void setGSplatRenderProxySplatData(GSplatRenderProxy* gsplatRenderProxy, std::vector<GPUSplatData>&& gpuSplatData);

        // 摘除这个 GSplat proxy 之后调用, 释放它的 splat 数据, 没有剩余 GSplat 时连 GSplatScene 一起释放
        void clearGSplatRenderProxySplatData(GSplatRenderProxy* gsplatRenderProxy);

        GSplatScene* getGSplatScene() const noexcept
        {
            return mGSplatScene.get();
        }

        // 这个场景要不要用 Lumen(Mesh Card Surface Cache)——显式开关, 不是"有没有 mesh
        // 烘焙过 Card 数据"这种隐式推导出来的状态。真正的创建(两张 2048x2048 atlas 纹理)
        // 延迟到下一次 update() 才发生, 不是调用这个函数的当下——这是唯一的创建
        // 入口, 时机固定、可预期, 不会因为哪个 Pass/哪个实例先摸到它而变。目前只支持开,
        // 不支持中途关闭后再释放资源(见 LumenScene.h 类注释, 跟 GSplatScene 不完全一样的
        // 地方那一段), enabled=false 只是不再创建新的, 已经创建过的 LumenScene 不会被销毁。
        // false->true 这条切换路径上, 把所有相机的全部 GDF cascade 标脏——关闭期间场景内容
        // 的变化不会被 markGlobalDistanceFieldDirty() 记录(它在没开 Lumen 时直接跳过), 重新
        // 打开时不知道具体漏了哪些更新, 只能全部重算一遍来补上
        void setLumenEnabled(bool enabled) noexcept;

        bool getLumenEnabled() const noexcept
        {
            return mLumenEnabled;
        }

        void setSceneVisualizeMode(SceneVisualizeMode mode) noexcept
        {
            mSceneVisualizeMode = mode;
        }

        SceneVisualizeMode getSceneVisualizeMode() const noexcept
        {
            return mSceneVisualizeMode;
        }

        // 跟 SceneVisualizeMode 正交的独立开关, 见 hyp::SceneVisualizeMode 的注释——渲染线程
        // 这份是唯一真正生效的, 由 GlobalDistanceFieldVisualizerPass 每帧读取
        void setUseMeshDistanceField(bool useMeshDistanceField) noexcept
        {
            mUseMeshDistanceField = useMeshDistanceField;
        }

        bool getUseMeshDistanceField() const noexcept
        {
            return mUseMeshDistanceField;
        }

        void setDisableScreenTrace(bool disableScreenTrace) noexcept
        {
            mDisableScreenTrace = disableScreenTrace;
        }

        bool getDisableScreenTrace() const noexcept
        {
            return mDisableScreenTrace;
        }

        // 给只想读、不想意外触发创建的消费方用(严格来说现在没有别的地方会创建它了, 但保留
        // "可能为空"的语义: Lumen 没启用, 或者启用了但还没轮到 update() 真正建出来)
        LumenScene* getLumenScene() const noexcept
        {
            return mLumenScene.get();
        }

        // 第一个蒙皮实例(SkeletalMeshRenderer)注册时才懒创建, 没有蒙皮网格的场景不占这块
        // 显存——跟 mLumenScene 的懒创建同一个道理, 只是没有 Lumen 那种显式 enabled 开关,
        // 谁第一个调用谁就触发创建
        SkinningManager& getOrCreateSkinningManager();

        // 给只想读、不想意外触发创建的消费方用
        SkinningManager* getSkinningManager() const noexcept
        {
            return mSkinningManager.get();
        }

        TRangeTable<MeshSectionEntry>& getSectionTable() noexcept
        {
            return *mSectionTable;
        }

        // 第一个四边形渲染器(文字/精灵)注册时才懒创建, 没有的场景不占这块显存
        QuadManager& getOrCreateQuadManager();

        // 纯 CPU 决策, 不碰 GPU——真正的提交见 prepare()。mLumenScene 的创建/开关生命周期
        // 归这里管(只有这里知道 mLumenEnabled 这个业务开关), card 分页/GDF cascade 这些
        // 具体决策已经分别下放给 CardCapturePass/GlobalDistanceFieldCompositePass 自己的
        // builder 去做, 这里不再统一编排
        void update();

        // 提交 update() 已经决定好的内容, 这个函数本身不做任何分配决策
        void prepare(rhi::CommandList& cmdList);

        std::vector<SceneViewGroup> gatherSceneViewGroups(ViewDataManager& viewDataManager, uint32_t frameIndex) const;

        uint32_t acquireInstanceIndex();

        void releaseInstanceIndex(uint32_t instanceIndex);

        uint32_t acquireLightIndex();

        void releaseLightIndex(uint32_t lightIndex);

        uint32_t acquireMeshInstanceIndex();

        void releaseMeshInstanceIndex(uint32_t meshInstanceIndex) noexcept;

    private:
        void createSkeletalDrawBindGroup();

        void createQuadDrawBindGroup();

        template <typename T>
        auto& getElementList() noexcept
        {
            if constexpr (std::is_same_v<T, MeshRenderProxy>) return mMeshRenderProxies;
            else if constexpr (std::is_same_v<T, QuadRenderProxy>) return mQuadRenderProxies;
            else if constexpr (std::is_same_v<T, GSplatRenderProxy>) return mGSplatRenderProxies;
            else if constexpr (std::is_same_v<T, CameraProxy>) return mCameraProxies;
            else if constexpr (std::is_same_v<T, DirectionalLightProxy>) return mDirectionalLightProxies;
            else if constexpr (std::is_same_v<T, PointLightProxy> || std::is_same_v<T, SpotLightProxy>) return mLocalLightProxies;
            else if constexpr (std::is_same_v<T, AtmosphereProxy>) return mAtmosphereProxies;
            else if constexpr (std::is_same_v<T, SkyLightProxy>) return mSkyLightProxies;
            else if constexpr (std::is_same_v<T, SkyBackgroundProxy>) return mSkyBackgroundProxies;
            else static_assert(sizeof(T) == 0, "不是场景元素 proxy 类型");
        }

        static void uploadData(
            rhi::CommandList& cmdList,
            std::span<const std::pair<size_t, size_t>> updatedRanges,
            size_t elementSize,
            uint8_t* srcData,
            rhi::Buffer* dstBuffer,
            StagingAllocator& stagingAllocator
        );

        void updateMeshBoundsIfDirty(Mobility mobility) const noexcept;

        void selectSunLights(bool requireAffectAtmosphere, DirectionalLightProxy* (&sunLights)[2]) const noexcept;

    private:
        uint32_t mSceneId = 0;
        const rhi::Buffer* mIndicesBuffer = nullptr;

        TRef<rhi::Buffer> mInstanceDataBuffer;
        TRef<rhi::Buffer> mLightDataBuffer;
        TRef<rhi::BindGroup> mInstanceDataBindGroup;
        TRef<rhi::BindGroup> mDrawBindGroup;
        TRef<rhi::BindGroup> mSkeletalDrawBindGroup;
        TRef<rhi::BindGroup> mQuadDrawBindGroup;

        TDirtySlotPool<InstanceData> mInstanceData{ kBaseInstanceCount };
        // 只记录真实的 Transform 变化，不包含上一帧运动停止后补写的 prev=current。
        // current 用来避免同一帧多次 setter 推进 prev；previous 用来在停止后的下一帧补一次上传。
        std::vector<uint64_t> mInstanceTransformUpdatedBitset;
        std::vector<uint64_t> mPrevInstanceTransformUpdatedBitset;

        TDirtySlotPool<LightData> mLightData{ kBaseLightCount };

        TRef<rhi::Buffer> mMeshInstanceDataBuffer;
        TDirtySlotPool<MeshInstanceData> mMeshInstanceData{ kBaseMeshInstanceCount };

        std::vector<MeshRenderProxy*> mMeshRenderProxies;
        std::vector<QuadRenderProxy*> mQuadRenderProxies;
        std::vector<CameraProxy*> mCameraProxies;
        std::vector<DirectionalLightProxy*> mDirectionalLightProxies;
        std::vector<LocalLightProxy*> mLocalLightProxies;
        std::vector<AtmosphereProxy*> mAtmosphereProxies;
        std::vector<SkyLightProxy*> mSkyLightProxies;
        std::vector<SkyBackgroundProxy*> mSkyBackgroundProxies;
        float3 mMainLightDirections[2] = { float3(0.0f, 0.0f, 1.0f), float3(0.0f, 0.0f, 1.0f) };
        float3 mMainLightIntensities[2] = { float3(0.0f), float3(0.0f) };

        std::unique_ptr<GSplatScene> mGSplatScene;
        std::vector<GSplatRenderProxy*> mGSplatRenderProxies;

        std::unique_ptr<LumenScene> mLumenScene;
        bool mLumenEnabled = false;
        SceneVisualizeMode mSceneVisualizeMode = SceneVisualizeMode::None;
        bool mUseMeshDistanceField = false;
        bool mDisableScreenTrace = false;

        std::unique_ptr<SkinningManager> mSkinningManager;
        std::vector<SkinningManager::BaseChange> mSkinningBaseChanges;
        std::unique_ptr<QuadManager> mQuadManager;
        std::unique_ptr<TRangeTable<MeshSectionEntry>> mSectionTable;

        mutable Boxd mStaticMeshBounds;
        mutable Boxd mDynamicMeshBounds;
        mutable bool mStaticMeshBoundsDirty = true;
        mutable bool mDynamicMeshBoundsDirty = true;

        static constexpr size_t kBaseInstanceCount = 512;
        static constexpr size_t kBaseLightCount = 16;
        static constexpr size_t kBaseMeshInstanceCount = 512;
        static constexpr uint32_t kBaseSkinningMatrixCapacity = 4096; // 先给个测试够用的默认值, 以后按实际场景规模调
        static constexpr uint32_t kBaseQuadElementCapacity = 16384; // 先给个测试够用的默认值, 以后按实际场景规模调
        static constexpr uint32_t kBaseQuadInstanceCapacity = 512;
        static constexpr uint32_t kBaseSectionTableCapacity = 4096;
    };

    template <>
    struct TProxyTraits<ProxyType::Scene>
    {
        using Proxy = SceneProxy;
    };
}
