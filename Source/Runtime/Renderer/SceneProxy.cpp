#include "SceneProxy.h"
#include "MeshRenderProxy.h"
#include "CameraProxy.h"
#include "DirectionalLightProxy.h"
#include "SkyLightProxy.h"

namespace hyp
{
    SceneProxy::SceneProxy(const ViewDataManager& viewDataManager, uint32_t sceneId) :
        mSceneId(sceneId)
    {
        auto& device = getRenderBackend().getDevice();

        {
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = kBaseIndexCount * sizeof(uint32_t);
            bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
            mIndicesBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
        }

        {
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = kBaseInstanceCount * sizeof(InstanceData);
            bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
            mInstanceDataBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
        }

        {
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = kBaseLightCount * sizeof(LightData);
            bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
            mLightDataBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
        }

        mSceneAndViewBindGroupLayout = createSceneAndViewBindGroupLayout(getRenderBackend().getBindGroupLayoutCache());
        mSceneAndViewBindGroup = device.createBindGroup(*mSceneAndViewBindGroupLayout);

        auto& viewDataBuffer = viewDataManager.getViewDataBuffer();

        rhi::BufferBindingInfo indicesBinding;
        indicesBinding.buffer = mIndicesBuffer.get();
        indicesBinding.offset = 0;
        indicesBinding.range = kBaseInstanceCount * sizeof(uint32_t);

        rhi::BufferBindingInfo instanceDataBinding;
        instanceDataBinding.buffer = mInstanceDataBuffer.get();
        instanceDataBinding.offset = 0;
        instanceDataBinding.range = kBaseInstanceCount * sizeof(InstanceData);

        rhi::BufferBindingInfo lightDataBinding;
        lightDataBinding.buffer = mLightDataBuffer.get();
        lightDataBinding.offset = 0;
        lightDataBinding.range = kBaseLightCount * sizeof(LightData);

        rhi::BufferBindingInfo viewDataBinding;
        viewDataBinding.buffer = &viewDataBuffer;
        viewDataBinding.offset = 0;
        viewDataBinding.range = sizeof(ViewData);

        rhi::BindGroupEntry entries[4] = {};
        entries[1].binding = 0;
        entries[1].type = rhi::BindingType::Storage_Buffer;
        entries[1].buffers = { &indicesBinding, 1 };

        entries[0].binding = 1;
        entries[0].type = rhi::BindingType::Storage_Buffer;
        entries[0].buffers = { &instanceDataBinding, 1 };

        entries[2].binding = 2;
        entries[2].type = rhi::BindingType::Storage_Buffer;
        entries[2].buffers = { &lightDataBinding, 1 };

        entries[3].binding = 3;
        entries[3].type = rhi::BindingType::Dynamic_Storage_Buffer;
        entries[3].buffers = { &viewDataBinding, 1 };

        mSceneAndViewBindGroup->update({ entries, 4 });

        mInstanceData.reserve(kBaseInstanceCount);
        mLightData.reserve(kBaseLightCount);
    }

    SceneProxy::~SceneProxy() noexcept
    {
        getRenderBackend().getDeferredTaskQueue().push(
            [indicesBuffer = std::move(mIndicesBuffer),
            instanceDataBuffer = std::move(mInstanceDataBuffer),
            lightDataBuffer = std::move(mLightDataBuffer),
            sceneAndViewBindGroup = std::move(mSceneAndViewBindGroup),
            sceneAndViewBindGroupLayout = std::move(mSceneAndViewBindGroupLayout)
            ]() mutable
            {
                sceneAndViewBindGroup = nullptr;
                sceneAndViewBindGroupLayout = nullptr;
                lightDataBuffer = nullptr;
                instanceDataBuffer = nullptr;
                indicesBuffer = nullptr;
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
        if (mActiveAtmosphereProxy == atmosphereProxy)
            mActiveAtmosphereProxy = nullptr;
    }

    void SceneProxy::addSkyLightProxy(SkyLightProxy* skyLightProxy)
    {
        mSkyLightProxies.push_back(skyLightProxy);
    }

    void SceneProxy::removeSkyLightProxy(SkyLightProxy* skyLightProxy)
    {
        auto it = std::find(mSkyLightProxies.begin(), mSkyLightProxies.end(), skyLightProxy);
        assert(it != mSkyLightProxies.end());
        mSkyLightProxies.erase(it);
        if (mActiveSkyLightProxy == skyLightProxy)
            mActiveSkyLightProxy = nullptr;
    }

    SkyLightProxy* SceneProxy::getActiveSkyLightProxy() const noexcept
    {
        SkyLightProxy* selected = nullptr;
        for (SkyLightProxy* candidate : mSkyLightProxies)
        {
            if (!candidate->isVisible())
                continue;

            if (!selected || candidate->getIntensity() > selected->getIntensity() + 1e-6f)
                selected = candidate;
        }
        mActiveSkyLightProxy = selected;
        return mActiveSkyLightProxy;
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

    void SceneProxy::updateSceneData(rhi::CommandList& cmdList)
    {
        auto& stagingAllocator = getRenderBackend().getStagingAllocator();

        auto updatedInstanceRanges = generateUpdateRanges(
            mInstanceDataUpdatedBitset,
            mUpdatedInstanceMinIndex,
            mUpdatedInstanceMaxIndex
        );

        auto updatedLightRanges = generateUpdateRanges(
            mLightDataUpdatedBitset,
            mUpdatedLightMinIndex,
            mUpdatedLightMaxIndex
        );

        std::vector<rhi::BufferBarrier> preBarriers;
        if (!updatedInstanceRanges.empty())
        {
            rhi::BufferBarrier barrier;
            barrier.buffer = mInstanceDataBuffer.get();
            barrier.offset = 0;
            barrier.size = rhi::kWholeBuffer;
            barrier.oldState = rhi::ResourceState::Shader_Storage_Read;
            barrier.newState = rhi::ResourceState::Transfer_Dst;
            preBarriers.push_back(barrier);
        }

        if (!updatedLightRanges.empty())
        {
            rhi::BufferBarrier barrier;
            barrier.buffer = mLightDataBuffer.get();
            barrier.offset = 0;
            barrier.size = rhi::kWholeBuffer;
            barrier.oldState = rhi::ResourceState::Shader_Storage_Read;
            barrier.newState = rhi::ResourceState::Transfer_Dst;
            preBarriers.push_back(barrier);
        }

        if (!preBarriers.empty())
        {
            cmdList.insertBarriers(preBarriers);
        }

        if (!updatedInstanceRanges.empty())
        {
            uploadData(
                cmdList,
                updatedInstanceRanges,
                sizeof(InstanceData),
                (uint8_t*)(mInstanceData.data()),
                mInstanceDataBuffer.get(),
                stagingAllocator
            );
        }

        if (!updatedLightRanges.empty())
        {
            uploadData(
                cmdList,
                updatedLightRanges,
                sizeof(LightData),
                (uint8_t*)(mLightData.data()),
                mLightDataBuffer.get(),
                stagingAllocator
            );
        }

        std::vector<rhi::BufferBarrier> postBarriers;
        if (!updatedInstanceRanges.empty())
        {
            rhi::BufferBarrier barrier;
            barrier.buffer = mInstanceDataBuffer.get();
            barrier.offset = 0;
            barrier.size = rhi::kWholeBuffer;
            barrier.oldState = rhi::ResourceState::Transfer_Dst;
            barrier.newState = rhi::ResourceState::Shader_Storage_Read;
            postBarriers.push_back(barrier);

            std::fill(mInstanceDataUpdatedBitset.begin(), mInstanceDataUpdatedBitset.end(), 0);
            mUpdatedInstanceMinIndex = UINT32_MAX;
            mUpdatedInstanceMaxIndex = 0;
        }

        if (!updatedLightRanges.empty())
        {
            rhi::BufferBarrier barrier;
            barrier.buffer = mLightDataBuffer.get();
            barrier.offset = 0;
            barrier.size = rhi::kWholeBuffer;
            barrier.oldState = rhi::ResourceState::Transfer_Dst;
            barrier.newState = rhi::ResourceState::Shader_Storage_Read;
            postBarriers.push_back(barrier);

            std::fill(mLightDataUpdatedBitset.begin(), mLightDataUpdatedBitset.end(), 0);
            mUpdatedLightMinIndex = UINT32_MAX;
            mUpdatedLightMaxIndex = 0;
        }

        if (!postBarriers.empty())
        {
            cmdList.insertBarriers(postBarriers);
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
            for (auto* candidate : mDirectionalLightProxies)
            {
                uint32_t sunLightIndex = candidate->getAtmosphereSunLightIndex();
                if (!candidate->isVisible() ||
                    !candidate->isAffectAtmosphere() ||
                    sunLightIndex >= 2)
                    continue;

                auto*& selected = sunLights[sunLightIndex];
                if (!selected)
                {
                    selected = candidate;
                    continue;
                }

                float candidateIntensitySqr = Math::lengthSqr(candidate->getIntensity());
                float selectedIntensitySqr = Math::lengthSqr(selected->getIntensity());
                if (candidateIntensitySqr > selectedIntensitySqr + 1e-6f)
                {
                    selected = candidate;
                }
            }

            for (uint32_t i = 0; i < 2; ++i)
            {
                float3 direction = sunLights[i] ? -sunLights[i]->getDirection() : float3(0, 0, 1);
                float3 intensity = sunLights[i] ? sunLights[i]->getIntensity() : float3(0);

                mActiveAtmosphereProxy->setSunLight(i, direction, intensity);
            }
            
            mActiveAtmosphereProxy->prepare(cmdList);
        }

        if (SkyLightProxy* activeSkyLight = getActiveSkyLightProxy())
        {
            activeSkyLight->prepare(cmdList);
        }

        if (mGSplatScene)
        {
            mGSplatScene->prepare(cmdList);
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

    TRef<rhi::BindGroupLayout> SceneProxy::createSceneAndViewBindGroupLayout(BindGroupLayoutCache& cache)
    {
        std::vector<rhi::BindGroupLayoutEntry> entries(4);
        auto& indicesEntry = entries[0];
        indicesEntry.binding = 0;
        indicesEntry.type = rhi::BindingType::Storage_Buffer;
        indicesEntry.access = rhi::BindingAccess::Read_Only;
        indicesEntry.count = 1;
        indicesEntry.stageFlags = rhi::ShaderStage::All;

        auto& instanceDataEntry = entries[1];
        instanceDataEntry.binding = 1;
        instanceDataEntry.type = rhi::BindingType::Storage_Buffer;
        instanceDataEntry.access = rhi::BindingAccess::Read_Only;
        instanceDataEntry.count = 1;
        instanceDataEntry.stageFlags = rhi::ShaderStage::All;

        auto& lightDataEntry = entries[2];
        lightDataEntry.binding = 2;
        lightDataEntry.type = rhi::BindingType::Storage_Buffer;
        lightDataEntry.access = rhi::BindingAccess::Read_Only;
        lightDataEntry.count = 1;
        lightDataEntry.stageFlags = rhi::ShaderStage::All;

        auto& viewDataEntry = entries[3];
        viewDataEntry.binding = 3;
        viewDataEntry.type = rhi::BindingType::Dynamic_Storage_Buffer;
        viewDataEntry.access = rhi::BindingAccess::Read_Only;
        viewDataEntry.count = 1;
        viewDataEntry.stageFlags = rhi::ShaderStage::All;

        return cache.getOrCreate(entries);
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

        uint32_t lastDirtyIndex = 0;

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
                    if (firstInWord - lastDirtyIndex > kMaxHoleInInstances)
                    {
                        ranges.emplace_back(currentStart, lastDirtyIndex - currentStart + 1);
                        currentStart = firstInWord;
                    }
                }
                lastDirtyIndex = lastInWord;
            }
        }

        if (currentStart != UINT32_MAX)
        {
            ranges.emplace_back(currentStart, lastDirtyIndex - currentStart + 1);
        }

        return ranges;
    }

    void SceneProxy::uploadData(
        rhi::CommandList& cmdList,
        std::span<const std::pair<size_t, size_t>> updatedRanges,
        size_t elementSize,
        uint8_t* srcData,
        rhi::Buffer* dstBuffer,
        StagingAllocator& stagingAllocator
    )
    {
        std::vector<rhi::BufferCopyRegion> bufferCopyRegions;
        bufferCopyRegions.reserve(updatedRanges.size());
        size_t totalSize = 0;
        for (auto& range : updatedRanges)
        {
            totalSize += range.second * elementSize;
        }
        auto allocation = stagingAllocator.forceAllocate(totalSize);
        size_t srcOffset = 0;
        for (auto& range : updatedRanges)
        {
            auto& copyRegion = bufferCopyRegions.emplace_back();
            size_t rangeSize = range.second * elementSize;
            copyRegion.srcOffset = allocation.offset + srcOffset;
            copyRegion.dstOffset = range.first * elementSize;
            copyRegion.size = rangeSize;
            std::memcpy((uint8_t*)(allocation.mappedData) + srcOffset, srcData + range.first * elementSize, rangeSize);
            srcOffset += rangeSize;
        }
        cmdList.copyBuffer(allocation.buffer, dstBuffer, bufferCopyRegions);
    }

}
