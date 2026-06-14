#include "SceneProxy.h"
#include "MeshRenderProxy.h"
#include "CameraProxy.h"
#include "DirectionalLightProxy.h"

namespace hyp
{
    SceneProxy::SceneProxy(const ViewDataManager& viewDataManager, uint32_t sceneId) :
        mSceneId(sceneId)
    {
        auto& device = getRenderBackend().getDevice();

        {
            vk::Buffer::CreateInfo bufferInfo;
            bufferInfo.size = kBaseInstanceCount * sizeof(InstanceData);
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mInstanceDataBuffer = new vk::Buffer(device, bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);
        }

        {
            vk::Buffer::CreateInfo bufferInfo;
            // 可能在一个 RenderPass 中绘制多次场景中的对象, 因此InstanceIndicesBuffer需要翻倍
            bufferInfo.size = kBaseInstanceCount * sizeof(uint32_t) * kInstanceIndicesMultiple;
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mInstanceIndicesBuffer = new vk::Buffer(device, bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);
        }

        {
            vk::Buffer::CreateInfo bufferInfo;
            bufferInfo.size = kBaseLightCount * sizeof(LightData);
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mLightDataBuffer = new vk::Buffer(device, bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);
        }

        TRef<vk::DescriptorSetLayout> sceneAndViewDescriptorSetLayout = createSceneAndViewDescriptorSetLayout(device);
        mSceneAndViewDescriptorSet = device.getDescriptorAllocator().allocate(*sceneAndViewDescriptorSetLayout);
        vk::DescriptorWriter writer;
        writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mInstanceDataBuffer->getHandle(), 0, kBaseInstanceCount * sizeof(InstanceData));
        writer.writeBuffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mInstanceIndicesBuffer->getHandle(), 0, kBaseInstanceCount * sizeof(uint32_t));
        writer.writeBuffer(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mLightDataBuffer->getHandle(), 0, kBaseLightCount * sizeof(LightData));
        auto& viewDataBuffer = viewDataManager.getViewDataBuffer();
        writer.writeBuffer(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, viewDataBuffer.getHandle(), 0, sizeof(ViewData));
        writer.update(*mSceneAndViewDescriptorSet);

        mInstanceData.reserve(kBaseInstanceCount);
    }

    SceneProxy::~SceneProxy() noexcept
    {
        getRenderBackend().getDeferredTaskQueue().push(
            [instanceDataBuffer = std::move(mInstanceDataBuffer),
            instanceIndicesBuffer = std::move(mInstanceIndicesBuffer),
            lightDataBuffer = std::move(mLightDataBuffer),
            sceneAndViewDescriptorSet = std::move(mSceneAndViewDescriptorSet)
            ]() mutable
            {
                sceneAndViewDescriptorSet = nullptr;
                instanceIndicesBuffer = nullptr;
                lightDataBuffer = nullptr;
                instanceDataBuffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void SceneProxy::notifyInstanceDataUpdated(uint32_t instanceIndex) noexcept
    {
        mUpdatedInstanceMinIndex = std::min(instanceIndex, mUpdatedInstanceMinIndex);
        mUpdatedInstanceMaxIndex = std::max(instanceIndex, mUpdatedInstanceMaxIndex);
        assert((instanceIndex / 64) < mInstanceData.size());
        mInstanceDataUpdatedBitset[instanceIndex / 64] |= 1ull << (instanceIndex % 64);
    }

    void SceneProxy::notifyLightDataUpdated(uint32_t lightIndex) noexcept
    {
        mUpdatedLightMinIndex = std::min(lightIndex, mUpdatedLightMinIndex);
        mUpdatedLightMaxIndex = std::max(lightIndex, mUpdatedLightMaxIndex);
        assert((lightIndex / 64) < mLightData.size());
        mLightDataUpdatedBitset[lightIndex / 64] |= 1ull << (lightIndex % 64);
    }

    void SceneProxy::addMeshRenderProxy(MeshRenderProxy* meshRenderProxy)
    {
        uint32_t instanceIndex = acquireInstanceIndex();
        meshRenderProxy->setInstanceIndex(instanceIndex);
        mMeshRenderProxies.push_back(meshRenderProxy);
    }

    void SceneProxy::removeMeshRenderProxy(MeshRenderProxy* meshRenderProxy)
    {
        releaseInstanceIndex(meshRenderProxy->getInstanceIndex());
        auto it = std::find(mMeshRenderProxies.begin(), mMeshRenderProxies.end(), meshRenderProxy);
        assert(it != mMeshRenderProxies.end());
        mMeshRenderProxies.erase(it);
    }

    void SceneProxy::addCameraProxy(CameraProxy* cameraProxy)
    {
        mCameraProxies.push_back(cameraProxy);
    }

    void SceneProxy::removeCameraProxy(CameraProxy* cameraProxy)
    {
        auto it = std::find(mCameraProxies.begin(), mCameraProxies.end(), cameraProxy);
        assert(it != mCameraProxies.end());
        mCameraProxies.erase(it);
    }

    void SceneProxy::addDirectionalLightProxy(DirectionalLightProxy* directionalLightProxy)
    {
        uint32_t lightIndex = acquireLightIndex();
        directionalLightProxy->setLightIndex(lightIndex);
        mDirectionalLightProxies.push_back(directionalLightProxy);
    }

    void SceneProxy::removeDirectionalLightProxy(DirectionalLightProxy* directionalLightProxy)
    {
        releaseLightIndex(directionalLightProxy->getLightIndex());
        auto it = std::find(mDirectionalLightProxies.begin(), mDirectionalLightProxies.end(), directionalLightProxy);
        assert(it != mDirectionalLightProxies.end());
        mDirectionalLightProxies.erase(it);
    }

    void SceneProxy::addAtmosphereProxy(AtmosphereProxy* atmosphereProxy)
    {
        mAtmosphereProxies.push_back(atmosphereProxy);
        if (!mActiveAtmosphereProxy && atmosphereProxy->isVisible())
        {
            mActiveAtmosphereProxy = atmosphereProxy;
        }
    }

    void SceneProxy::removeAtmosphereProxy(AtmosphereProxy* atmosphereProxy)
    {
        auto it = std::find(mAtmosphereProxies.begin(), mAtmosphereProxies.end(), atmosphereProxy);
        assert(it != mAtmosphereProxies.end());
        mAtmosphereProxies.erase(it);
    }

    void SceneProxy::addGSplatRenderProxy(GSplatRenderProxy* gsplatRenderProxy)
    {
        uint32_t instanceIndex = acquireInstanceIndex();
        gsplatRenderProxy->setInstanceIndex(instanceIndex);
        mGSplatRenderProxies.push_back(gsplatRenderProxy);
    }

    void SceneProxy::setGSplatRenderProxySplatData(GSplatRenderProxy* gsplatRenderProxy, std::vector<GPUSplatData>&& gpuSplatData)
    {
        auto it = std::find(mGSplatRenderProxies.begin(), mGSplatRenderProxies.end(), gsplatRenderProxy);
        if (it == mGSplatRenderProxies.end())
            return;

        if (!mGSplatScene)
        {
            mGSplatScene = std::make_unique<GSplatScene>(getRenderBackend());
        }

        mGSplatScene->removeGSplat(gsplatRenderProxy->getGSplatId());
        uint32_t instanceIndex = gsplatRenderProxy->getInstanceIndex();
        for (auto& splat : gpuSplatData)
        {
            splat.instanceIndex = instanceIndex;
        }
        uint32_t gsplatId = mGSplatScene->addGSplat(std::move(gpuSplatData));
        gsplatRenderProxy->setGSplatId(gsplatId);
    }

    void SceneProxy::removeGSplatRenderProxy(GSplatRenderProxy* gsplatRenderProxy)
    {
        releaseInstanceIndex(gsplatRenderProxy->getInstanceIndex());
        mGSplatScene->removeGSplat(gsplatRenderProxy->getGSplatId());
        auto it = std::find(mGSplatRenderProxies.begin(), mGSplatRenderProxies.end(), gsplatRenderProxy);
        assert(it != mGSplatRenderProxies.end());
        mGSplatRenderProxies.erase(it);

        if (mGSplatRenderProxies.empty())
        {
            mGSplatScene = nullptr;
        }
    }

    void SceneProxy::updateSceneData(vk::CommandBuffer& cmd)
    {
        auto& stagingAllocator = getRenderBackend().getStagingAllocator();

        auto updatedInstanceRanges = generateUpdateRanges(
            mInstanceDataUpdatedBitset,
            mUpdatedInstanceMinIndex,
            mUpdatedInstanceMaxIndex
        );

        for (auto& range : updatedInstanceRanges)
        {
            size_t offset = range.first * sizeof(InstanceData);
            size_t dataSize = range.second * sizeof(InstanceData);
            uploadData(cmd, mInstanceDataBuffer->getHandle(), offset, { (uint8_t*)(&mInstanceData[range.first]), dataSize }, stagingAllocator);
        }

        auto updatedLightRanges = generateUpdateRanges(
            mLightDataUpdatedBitset,
            mUpdatedLightMinIndex,
            mUpdatedLightMaxIndex
        );

        for (auto& range : updatedLightRanges)
        {
            size_t offset = range.first * sizeof(LightData);
            size_t dataSize = range.second * sizeof(LightData);
            uploadData(cmd, mLightDataBuffer->getHandle(), offset, { (uint8_t*)(&mLightData[range.first]), dataSize }, stagingAllocator);
        }

        VkBufferMemoryBarrier2 barriers[2] = {};
        uint32_t barrierCount = 0;
        if (!updatedInstanceRanges.empty())
        {
            VkBufferMemoryBarrier2& barrier = barriers[barrierCount++];
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.buffer = mInstanceDataBuffer->getHandle();
            size_t beginOffset = updatedInstanceRanges.begin()->first;
            size_t endOffset = updatedInstanceRanges.back().first;
            size_t endSize = updatedInstanceRanges.back().second;
            barrier.offset = beginOffset * sizeof(InstanceData);
            barrier.size = (endOffset - beginOffset + endSize) * sizeof(InstanceData);

            std::fill(mInstanceDataUpdatedBitset.begin(), mInstanceDataUpdatedBitset.end(), 0);
            mUpdatedInstanceMinIndex = UINT32_MAX;
            mUpdatedInstanceMaxIndex = 0;
        }

        if (!updatedLightRanges.empty())
        {
            VkBufferMemoryBarrier2& barrier = barriers[barrierCount++];
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.buffer = mLightDataBuffer->getHandle();
            size_t beginOffset = updatedLightRanges.begin()->first;
            size_t endOffset = updatedLightRanges.back().first;
            size_t endSize = updatedLightRanges.back().second;
            barrier.offset = beginOffset * sizeof(LightData);
            barrier.size = (endOffset - beginOffset + endSize) * sizeof(LightData);

            std::fill(mLightDataUpdatedBitset.begin(), mLightDataUpdatedBitset.end(), 0);
            mUpdatedLightMinIndex = UINT32_MAX;
            mUpdatedLightMaxIndex = 0;
        }

        if (barrierCount)
        {
            cmd.pipelineBarrier2({ &barriers[0], barrierCount });
        }

        for (AtmosphereProxy* proxy : mAtmosphereProxies)
        {
            if (mActiveAtmosphereProxy != proxy && proxy->isVisible())
            {
                mActiveAtmosphereProxy = proxy;
                break;
            }
        }

        if (mActiveAtmosphereProxy)
        {
            DirectionalLightProxy* sunLights[2] = { nullptr, nullptr };
            for (auto* proxy : mDirectionalLightProxies)
            {
                if (!proxy->isVisible() || !proxy->isAffectAtmosphere())
                    continue;

                uint32_t idx = proxy->getAtmosphereSunLightIndex();
                if (idx > 1)
                    continue;

                if (sunLights[idx] == nullptr)
                {
                    sunLights[idx] = proxy;
                    continue;
                }

                float newIntensitySqr = Math::lengthSqr(proxy->getIntensity());
                float oldIntensitySqr = Math::lengthSqr(sunLights[idx]->getIntensity());

                bool shouldReplace = false;

                if (newIntensitySqr > oldIntensitySqr)
                {
                    shouldReplace = true;
                }
                else if (std::abs(newIntensitySqr - oldIntensitySqr) < 1e-6f)
                {
                    if (proxy->getAtmosphereRequestOrder() < sunLights[idx]->getAtmosphereRequestOrder())
                        shouldReplace = true;
                }

                if (!shouldReplace)
                {
                    sunLights[idx] = proxy;
                }
            }

            for (uint32_t i = 0; i < 2; ++i)
            {
                float3 direction = sunLights[i] ? -sunLights[i]->getDirection() : float3(0, 0, 1);
                float3 intensity = sunLights[i] ? sunLights[i]->getIntensity() : float3(0);

                mActiveAtmosphereProxy->setSunLight(i, direction, intensity);
            }
            
            mActiveAtmosphereProxy->prepare(cmd);
        }

        if (mGSplatScene)
        {
            mGSplatScene->prepare(cmd);
        }
    }

    std::vector<SceneViewGroup> SceneProxy::gatherSceneViewGroups(ViewDataManager& viewDataManager) const
    {
        std::vector<SceneViewGroup> sceneViewGroups;
        size_t expectedSceneViewCount = mDirectionalLightProxies.size() * mCameraProxies.size() + mCameraProxies.size();
        sceneViewGroups.reserve(expectedSceneViewCount);
        for (auto directionalLightProxy : mDirectionalLightProxies)
        {
            if (!directionalLightProxy->isCastShadows())
                continue;

            auto shadowSceneViewGroups = directionalLightProxy->getShadowSceneViewGroups(mCameraProxies, viewDataManager);

            sceneViewGroups.insert(
                sceneViewGroups.end(),
                std::make_move_iterator(shadowSceneViewGroups.begin()),
                std::make_move_iterator(shadowSceneViewGroups.end())
            );
        }
        for (auto cameraProxy : mCameraProxies)
        {
            sceneViewGroups.emplace_back(cameraProxy->getSceneViewGroup());
        }
        return sceneViewGroups;
    }

    TRef<vk::DescriptorSetLayout> SceneProxy::createSceneAndViewDescriptorSetLayout(vk::Device& device)
    {
        VkDescriptorSetLayoutBinding bindings[4] = {};
        // Instance data
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_ALL;
        bindings[0].pImmutableSamplers = nullptr;

        // Instance Indices
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_ALL;
        bindings[1].pImmutableSamplers = nullptr;

        // Light Data
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_ALL;
        bindings[2].pImmutableSamplers = nullptr;

        // View Data
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_ALL;
        bindings[3].pImmutableSamplers = nullptr;

        return device.getDescriptorAllocator().getSetLayout(bindings);
    }

    inline int ctz64(uint64_t x)
    {
        if (x == 0) return 64;
#if defined(_MSC_VER)
        unsigned long index;
        _BitScanForward64(&index, x);
        return int(index);
#else
        return __builtin_ctzll(x);
#endif
    }

    inline int clz64(uint64_t x) {
        if (x == 0) return 64;
#if defined(_MSC_VER)
        unsigned long index;
        _BitScanReverse64(&index, x);
        return 63 - int(index);
#else
        return __builtin_clzll(x);
#endif
    }

    uint32_t SceneProxy::acquireInstanceIndex()
    {
        uint32_t instanceIndex;
        if (mFreeInstanceIndices.empty())
        {
            instanceIndex = static_cast<uint32_t>(mInstanceData.size());
            mInstanceData.push_back(InstanceData{});
            mInstanceDataUpdatedBitset.resize((mInstanceData.size() + 64 - 1) / 64, 0);
        }
        else
        {
            instanceIndex = mFreeInstanceIndices.back();
            mFreeInstanceIndices.pop_back();
        }
        return instanceIndex;
    }

    void SceneProxy::releaseInstanceIndex(uint32_t instanceIndex)
    {
        mFreeInstanceIndices.push_back(instanceIndex);
    }

    uint32_t SceneProxy::acquireLightIndex()
    {
        uint32_t lightIndex;
        if (mFreeLightIndices.empty())
        {
            lightIndex = static_cast<uint32_t>(mLightData.size());
            mLightData.push_back(LightData{});
            mLightDataUpdatedBitset.resize((mLightData.size() + 64 - 1) / 64, 0);
        }
        else
        {
            lightIndex = mFreeLightIndices.back();
            mFreeLightIndices.pop_back();
        }
        return lightIndex;
    }

    void SceneProxy::releaseLightIndex(uint32_t lightIndex)
    {
        mFreeLightIndices.push_back(lightIndex);
    }

    std::vector<std::pair<size_t, size_t>> SceneProxy::generateUpdateRanges(
        const std::vector<uint64_t>& updatedBitset,
        uint32_t minUpdatedIndex,
        uint32_t maxUpdatedIndex
    )
    {
        std::vector<std::pair<size_t, size_t>> ranges;
        if (updatedBitset.empty()) return ranges;

        static constexpr size_t kBitPerWord = 64;
        static constexpr size_t kMaxHoleInInstances = 64;
        uint32_t currentStart = UINT32_MAX;
        uint32_t prevIndex = 0;

        uint32_t minWord = minUpdatedIndex / kBitPerWord;
        uint32_t maxWord = maxUpdatedIndex / kBitPerWord;

        uint32_t lastDirtyIndex = 0; // 记录最后一个真正的脏位

        for (uint32_t w = minWord; w <= maxWord; ++w)
        {
            uint64_t word = updatedBitset[w];
            if (word != 0)
            {
                uint32_t firstInWord = w * kBitPerWord + ctz64(word);
                uint32_t lastInWord = w * kBitPerWord + (63 - clz64(word));

                if (currentStart == UINT32_MAX)
                {
                    currentStart = firstInWord;
                }
                else
                {
                    // 核心：基于实例数量判断是否断开
                    if (firstInWord - lastDirtyIndex > kMaxHoleInInstances)
                    {
                        ranges.emplace_back(currentStart, lastDirtyIndex - currentStart + 1);
                        currentStart = firstInWord;
                    }
                }
                lastDirtyIndex = lastInWord;
            }
        }

        // 结尾逻辑只需处理 lastDirtyIndex
        if (currentStart != UINT32_MAX)
        {
            ranges.emplace_back(currentStart, lastDirtyIndex - currentStart + 1);
        }

        return ranges;
    }

    void SceneProxy::uploadData(
        vk::CommandBuffer& cmd,
        VkBuffer dstBuffer,
        size_t dstOffset,
        std::span<const uint8_t> srcData,
        StagingAllocator& stagingAllocator
    )
    {
        if (srcData.size() <= 4096)
        {
            cmd.updateBuffer(
                dstBuffer,
                dstOffset,
                srcData.size(),
                srcData.data()
            );
        }
        else
        {
            auto allocation = stagingAllocator.forceAllocate(srcData.size());
            std::memcpy(allocation.mappedData, srcData.data(), srcData.size());

            VkBufferCopy copyRegion = {};
            copyRegion.srcOffset = allocation.offset;
            copyRegion.dstOffset = dstOffset;
            copyRegion.size = srcData.size();

            cmd.copyBuffer(
                allocation.buffer,
                dstBuffer,
                { &copyRegion, 1 }
            );
        }
    }

}
