#pragma once
#include "SceneView.h"
#include "CameraProxy.h"
#include "MeshRenderProxy.h"
#include "DirectionalLightProxy.h"
#include "AtmosphereProxy.h"
#include "GSplatRenderProxy.h"
#include "GSplatScene.h"

#include <RenderBase/Vulkan/Buffer.h>
#include <RenderBase/RenderBaseDefines.h>

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

        const vk::Buffer& getInstanceIndicesBuffer() const noexcept
        {
            return *mInstanceIndicesBuffer;
        }

        const vk::DescriptorSet& getSceneAndViewDescriptorSet() const noexcept
        {
            return *mSceneAndViewDescriptorSet;
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
            uint2 padding0;
            float3 position;
            float padding1;
            float3 direction;
            float padding2;
            float3 intensity;
            float padding3;
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

        void addGSplatRenderProxy(GSplatRenderProxy* gsplatRenderProxy);

        void setGSplatRenderProxySplatData(GSplatRenderProxy* gsplatRenderProxy, std::vector<GPUSplatData>&& gpuSplatData);

        void removeGSplatRenderProxy(GSplatRenderProxy* gsplatRenderProxy);

        GSplatScene* getGSplatScene() const noexcept
        {
            return mGSplatScene.get();
        }

        void updateSceneData(vk::CommandBuffer& cmd);

        std::vector<SceneViewGroup> gatherSceneViewGroups(ViewDataManager& viewDataManager) const;

        static TRef<vk::DescriptorSetLayout> createSceneAndViewDescriptorSetLayout(vk::Device& device);

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
            vk::CommandBuffer& cmd,
            VkBuffer dstBuffer,
            size_t dstOffset,
            std::span<const uint8_t> srcData,
            StagingAllocator& stagingAllocator
        );

    private:
        uint32_t mSceneId = 0;

        TRef<vk::Buffer> mInstanceDataBuffer;
        TRef<vk::Buffer> mInstanceIndicesBuffer;
        TRef<vk::Buffer> mLightDataBuffer;
        TRef<vk::DescriptorSet> mSceneAndViewDescriptorSet;

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

        std::unique_ptr<GSplatScene> mGSplatScene;
        std::vector<GSplatRenderProxy*> mGSplatRenderProxies;

        static constexpr size_t kBaseInstanceCount = 512;
        static constexpr size_t kInstanceIndicesMultiple = 4;
        static constexpr size_t kBaseLightCount = 16;
    };

    template <>
    struct TProxyTraits<ProxyType::Scene>
    {
        using Proxy = SceneProxy;
    };
}
