#pragma once
#include "SceneView.h"
#include "CameraProxy.h"
#include "MeshRenderProxy.h"
#include "DirectionalLightProxy.h"
#include "AtmosphereProxy.h"
#include "SkyLightProxy.h"
#include "GSplatRenderProxy.h"
#include "GSplatScene.h"

#include <RenderBase/RenderProxy.h>
#include <RHI/Buffer.h>
#include <RHI/BindGroup.h>

namespace hyp
{
    // TODO: 增加 SceneId, 用于区分 Per Scene 资源

    class SceneProxy : public RenderProxy
    {
    public:
        SceneProxy(const ViewDataManager& viewDataManager, uint32_t sceneId);

        ~SceneProxy() noexcept;

        struct InstanceData
        {
            WorldMatrix localToWorld;
            float3x4 localToWorldRot;
        };

        uint32_t getSceneId() const noexcept
        {
            return mSceneId;
        }

        const rhi::Buffer& getIndicesBuffer() const noexcept
        {
            return *mIndicesBuffer;
        }

        // TranslucentPass 需要在自己专属的 scene-and-view bind group 里(比公共那份多一张
        // LightVolume 纹理)复用这两个 buffer——它们本来就是 SceneProxy 自己拥有并且已经在用
        // 的资源, 这里只是多开一个只读入口, 不代表 SceneProxy 对光照体积纹理本身有任何感知
        const rhi::Buffer& getInstanceDataBuffer() const noexcept
        {
            return *mInstanceDataBuffer;
        }

        const rhi::Buffer& getLightDataBuffer() const noexcept
        {
            return *mLightDataBuffer;
        }

        const rhi::BindGroup& getSceneAndViewBindGroup() const noexcept
        {
            return *mSceneAndViewBindGroup;
        }

        InstanceData& getInstanceData(uint32_t instanceIndex) noexcept
        {
            assert(instanceIndex < mInstanceData.size());
            return mInstanceData[instanceIndex];
        }

        const InstanceData& getInstanceData(uint32_t instanceIndex) const noexcept
        {
            assert(instanceIndex < mInstanceData.size());
            return mInstanceData[instanceIndex];
        }

        void notifyInstanceDataUpdated(uint32_t instanceIndex) noexcept;

        enum class LightType : uint32_t
        {
            Directional,
            Point,
            Spot,
        };

        struct LightData
        {
            LightType type;
            uint32_t castShadows;
            uint32_t instanceIndex;
            uint32_t padding0;
            float3 intensity;
            uint32_t padding1;
        };

        LightData& getLightData(uint32_t lightIndex) noexcept
        {
            assert(lightIndex < mLightData.size());
            return mLightData[lightIndex];
        }

        const LightData& getLightData(uint32_t lightIndex) const noexcept
        {
            assert(lightIndex < mLightData.size());
            return mLightData[lightIndex];
        }

        void notifyLightDataUpdated(uint32_t lightIndex) noexcept;

        void addMeshRenderProxy(MeshRenderProxy* meshRenderProxy);

        void removeMeshRenderProxy(MeshRenderProxy* meshRenderProxy);

        std::span<MeshRenderProxy* const> getMeshRenderProxies() const noexcept
        {
            return mMeshRenderProxies;
        }

        void addCameraProxy(CameraProxy* cameraProxy);

        void removeCameraProxy(CameraProxy* cameraProxy);

        std::span<CameraProxy* const> getCameraProxies() const noexcept
        {
            return mCameraProxies;
        }

        void addDirectionalLightProxy(DirectionalLightProxy* directionalLightProxy);

        void removeDirectionalLightProxy(DirectionalLightProxy* directionalLightProxy);

        std::span<DirectionalLightProxy* const> getDirectionalLightProxies() const noexcept
        {
            return mDirectionalLightProxies;
        }

        void addAtmosphereProxy(AtmosphereProxy* atmosphereProxy);

        void removeAtmosphereProxy(AtmosphereProxy* atmosphereProxy);

        std::span<AtmosphereProxy* const> getAtmosphereProxies() const noexcept
        {
            return mAtmosphereProxies;
        }

        AtmosphereProxy* getActiveAtmosphereProxy() const noexcept
        {
            return mActiveAtmosphereProxy;
        }

        void addSkyLightProxy(SkyLightProxy* skyLightProxy);

        void removeSkyLightProxy(SkyLightProxy* skyLightProxy);

        std::span<SkyLightProxy* const> getSkyLightProxies() const noexcept
        {
            return mSkyLightProxies;
        }

        SkyLightProxy* getActiveSkyLightProxy() const noexcept;

        void addGSplatRenderProxy(GSplatRenderProxy* gsplatRenderProxy);

        void setGSplatRenderProxySplatData(GSplatRenderProxy* gsplatRenderProxy, std::vector<GPUSplatData>&& gpuSplatData);

        void removeGSplatRenderProxy(GSplatRenderProxy* gsplatRenderProxy);

        GSplatScene* getGSplatScene() const noexcept
        {
            return mGSplatScene.get();
        }

        void updateSceneData(rhi::CommandList& cmdList);

        std::vector<SceneViewGroup> gatherSceneViewGroups(ViewDataManager& viewDataManager) const;

        static TRef<rhi::BindGroupLayout> createSceneAndViewBindGroupLayout(BindGroupLayoutCache& cache);

    private:
        uint32_t acquireInstanceIndex();

        void releaseInstanceIndex(uint32_t instanceIndex);

        uint32_t acquireLightIndex();

        void releaseLightIndex(uint32_t lightIndex);

        static std::vector<std::pair<size_t, size_t>> generateUpdateRanges(
            const std::vector<uint64_t>& updatedBitset,
            uint32_t minUpdatedIndex,
            uint32_t maxUpdatedIndex
        );

        static void uploadData(
            rhi::CommandList& cmdList,
            std::span<const std::pair<size_t, size_t>> updatedRanges,
            size_t elementSize,
            uint8_t* srcData,
            rhi::Buffer* dstBuffer,
            StagingAllocator& stagingAllocator
        );

    private:
        uint32_t mSceneId = 0;

        TRef<rhi::Buffer> mIndicesBuffer;
        TRef<rhi::Buffer> mInstanceDataBuffer;
        TRef<rhi::Buffer> mLightDataBuffer;
        TRef<rhi::BindGroupLayout> mSceneAndViewBindGroupLayout;
        TRef<rhi::BindGroup> mSceneAndViewBindGroup;

        std::vector<InstanceData> mInstanceData;
        std::vector<uint32_t> mFreeInstanceIndices;
        std::vector<uint64_t> mInstanceDataUpdatedBitset;
        uint32_t mUpdatedInstanceMinIndex = UINT32_MAX;
        uint32_t mUpdatedInstanceMaxIndex = 0;

        std::vector<LightData> mLightData;
        std::vector<uint32_t> mFreeLightIndices;
        std::vector<uint64_t> mLightDataUpdatedBitset;
        uint32_t mUpdatedLightMinIndex = UINT32_MAX;
        uint32_t mUpdatedLightMaxIndex = 0;

        std::vector<MeshRenderProxy*> mMeshRenderProxies;
        std::vector<CameraProxy*> mCameraProxies;
        std::vector<DirectionalLightProxy*> mDirectionalLightProxies;
        std::vector<AtmosphereProxy*> mAtmosphereProxies;
        AtmosphereProxy* mActiveAtmosphereProxy = nullptr;
        std::vector<SkyLightProxy*> mSkyLightProxies;
        mutable SkyLightProxy* mActiveSkyLightProxy = nullptr;

        std::unique_ptr<GSplatScene> mGSplatScene;
        std::vector<GSplatRenderProxy*> mGSplatRenderProxies;

        static constexpr size_t kBaseIndexCount = 1024;
        static constexpr size_t kBaseInstanceCount = 512;
        static constexpr size_t kBaseLightCount = 16;
    };

    template <>
    struct TProxyTraits<ProxyType::Scene>
    {
        using Proxy = SceneProxy;
    };
}
