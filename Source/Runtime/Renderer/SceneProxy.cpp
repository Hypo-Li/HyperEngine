#include "SceneProxy.h"
#include "MeshRenderProxy.h"
#include "CameraProxy.h"
#include "DirectionalLightProxy.h"
#include "GlobalDistanceField.h"
#include "RendererContext.h"
#include "SkyLightProxy.h"

namespace hyp
{
    SceneProxy::SceneProxy(uint32_t sceneId, const rhi::Buffer& indicesBuffer) :
        mSceneId(sceneId),
        mIndicesBuffer(&indicesBuffer)
    {
        auto& device = getRenderBackend().getDevice();

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

        {
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = kBaseMeshInstanceCount * sizeof(MeshInstanceData);
            bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
            mMeshInstanceDataBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
        }

        mSectionTable = std::make_unique<TRangeTable<MeshSectionEntry>>(getRenderBackend(), kBaseSectionTableCapacity, "SceneProxy.SectionTable");

        auto& bindGroupLayoutCache = getRenderBackend().getBindGroupLayoutCache();

        rhi::BufferBindingInfo instanceDataBinding;
        instanceDataBinding.buffer = mInstanceDataBuffer.get();
        instanceDataBinding.offset = 0;
        instanceDataBinding.range = kBaseInstanceCount * sizeof(InstanceData);

        mInstanceDataBindGroup = device.createBindGroup(*RendererContext::createInstanceDataBindGroupLayout(bindGroupLayoutCache));
        rhi::BindGroupEntry instanceDataEntry = {};
        instanceDataEntry.binding = 0;
        instanceDataEntry.type = rhi::BindingType::Storage_Buffer;
        instanceDataEntry.buffers = { &instanceDataBinding, 1 };
        mInstanceDataBindGroup->update({ &instanceDataEntry, 1 });

        rhi::BufferBindingInfo indicesBinding;
        indicesBinding.buffer = mIndicesBuffer;
        indicesBinding.offset = 0;
        indicesBinding.range = mIndicesBuffer->getDesc().size;

        mDrawBindGroup = device.createBindGroup(*RendererContext::createDrawBindGroupLayout(bindGroupLayoutCache));
        rhi::BufferBindingInfo meshInstanceDataBinding;
        meshInstanceDataBinding.buffer = mMeshInstanceDataBuffer.get();
        meshInstanceDataBinding.offset = 0;
        meshInstanceDataBinding.range = kBaseMeshInstanceCount * sizeof(MeshInstanceData);

        rhi::BindGroupEntry drawEntries[3] = {};
        drawEntries[0] = instanceDataEntry;
        drawEntries[1].binding = 1;
        drawEntries[1].type = rhi::BindingType::Storage_Buffer;
        drawEntries[1].buffers = { &indicesBinding, 1 };
        drawEntries[2].binding = 2;
        drawEntries[2].type = rhi::BindingType::Storage_Buffer;
        drawEntries[2].buffers = { &meshInstanceDataBinding, 1 };
        mDrawBindGroup->update({ drawEntries, 3 });
    }

    SceneProxy::~SceneProxy() noexcept
    {
        assert(mMeshRenderProxies.empty() && mQuadRenderProxies.empty() && mGSplatRenderProxies.empty()
            && mCameraProxies.empty() && mDirectionalLightProxies.empty() && mLocalLightProxies.empty()
            && mAtmosphereProxies.empty() && mSkyLightProxies.empty() && mSkyBackgroundProxies.empty()
            && "场景元素的 proxy 必须先于 SceneProxy 销毁");

        getRenderBackend().getDeferredTaskQueue().push(
            [instanceDataBuffer = std::move(mInstanceDataBuffer),
            lightDataBuffer = std::move(mLightDataBuffer),
            meshInstanceDataBuffer = std::move(mMeshInstanceDataBuffer),
            instanceDataBindGroup = std::move(mInstanceDataBindGroup),
            drawBindGroup = std::move(mDrawBindGroup),
            skeletalDrawBindGroup = std::move(mSkeletalDrawBindGroup),
            quadDrawBindGroup = std::move(mQuadDrawBindGroup)
            ]() mutable
            {
                quadDrawBindGroup = nullptr;
                skeletalDrawBindGroup = nullptr;
                drawBindGroup = nullptr;
                instanceDataBindGroup = nullptr;
                lightDataBuffer = nullptr;
                instanceDataBuffer = nullptr;
                meshInstanceDataBuffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void SceneProxy::markInstanceDataUpdated(uint32_t instanceIndex) noexcept
    {
        mInstanceData.markUpdated(instanceIndex);
    }

    bool SceneProxy::isInstanceTransformUpdated(uint32_t instanceIndex) const noexcept
    {
        uint32_t wordIndex = instanceIndex / 64;
        assert(wordIndex < mInstanceTransformUpdatedBitset.size());
        return (mInstanceTransformUpdatedBitset[wordIndex] & (1ull << (instanceIndex % 64))) != 0;
    }

    void SceneProxy::markInstanceTransformUpdated(uint32_t instanceIndex) noexcept
    {
        markInstanceDataUpdated(instanceIndex);
        uint32_t wordIndex = instanceIndex / 64;
        assert(wordIndex < mInstanceTransformUpdatedBitset.size());
        mInstanceTransformUpdatedBitset[wordIndex] |= 1ull << (instanceIndex % 64);
    }

    void SceneProxy::markLightDataUpdated(uint32_t lightIndex) noexcept
    {
        mLightData.markUpdated(lightIndex);
    }

    void SceneProxy::markMeshInstanceDataUpdated(uint32_t meshInstanceIndex) noexcept
    {
        mMeshInstanceData.markUpdated(meshInstanceIndex);
    }

    uint32_t SceneProxy::acquireMeshInstanceIndex()
    {
        return mMeshInstanceData.acquire();
    }

    void SceneProxy::releaseMeshInstanceIndex(uint32_t meshInstanceIndex) noexcept
    {
        mMeshInstanceData.release(meshInstanceIndex);
    }

    Boxd SceneProxy::getSceneBounds() const noexcept
    {
        updateMeshBoundsIfDirty(Mobility::Static);
        updateMeshBoundsIfDirty(Mobility::Dynamic);
        return mStaticMeshBounds + mDynamicMeshBounds;
    }

    void SceneProxy::markMeshBoundsDirty(Mobility mobility) const noexcept
    {
        if (mobility == Mobility::Static)
            mStaticMeshBoundsDirty = true;
        else
            mDynamicMeshBoundsDirty = true;
    }

    void SceneProxy::setLumenEnabled(bool enabled) noexcept
    {
        if (mLumenEnabled == enabled)
            return;
        mLumenEnabled = enabled;

        if (enabled)
        {
            for (CameraProxy* cameraProxy : mCameraProxies)
            {
                for (GlobalDistanceField::Cascade& cascade : cameraProxy->getGlobalDistanceField().getCascades())
                {
                    cascade.staticLayer.dirty = true;
                    cascade.dynamicLayer.dirty = true;
                }
            }
        }
    }

    void SceneProxy::markGlobalDistanceFieldDirty(Boxd instanceWorldBounds, Mobility mobility) const noexcept
    {
        if (!mLumenEnabled)
            return;

        for (CameraProxy* cameraProxy : mCameraProxies)
        {
            for (GlobalDistanceField::Cascade& cascade : cameraProxy->getGlobalDistanceField().getCascades())
            {
                Boxd cascadeBounds(cascade.center - double3(cascade.halfExtent), cascade.center + double3(cascade.halfExtent));
                if (!cascadeBounds.intersect(instanceWorldBounds))
                    continue;

                // 动态层最终存的是"静态 ∪ 动态"的合并结果(见 GlobalDistanceFieldComposite.slang
                // 顶部注释)——静态候选变化会影响动态层该合并出来的内容, 必须连带标脏; 反过来
                // 动态候选变化不影响静态层, 不需要连带
                if (mobility == Mobility::Static)
                {
                    cascade.staticLayer.dirty = true;
                    cascade.dynamicLayer.dirty = true;
                }
                else
                {
                    cascade.dynamicLayer.dirty = true;
                }
            }
        }
    }

    void SceneProxy::updateMeshBoundsIfDirty(Mobility mobility) const noexcept
    {
        bool& dirty = mobility == Mobility::Static ? mStaticMeshBoundsDirty : mDynamicMeshBoundsDirty;
        if (!dirty)
            return;

        Boxd& bounds = mobility == Mobility::Static ? mStaticMeshBounds : mDynamicMeshBounds;
        bounds = Boxd();
        for (MeshRenderProxy* meshRenderProxy : mMeshRenderProxies)
        {
            if (meshRenderProxy->getMobility() == mobility)
                bounds += meshRenderProxy->getBoundingBox();
        }
        dirty = false;
    }

    SkyBackgroundProxy* SceneProxy::getActiveSkyBackgroundProxy() const noexcept
    {
        for (SkyBackgroundProxy* proxy : mSkyBackgroundProxies)
        {
            if (proxy->isVisible())
                return proxy;
        }
        return nullptr;
    }

    void SceneProxy::selectSunLights(bool requireAffectAtmosphere, DirectionalLightProxy* (&sunLights)[2]) const noexcept
    {
        sunLights[0] = nullptr;
        sunLights[1] = nullptr;
        for (auto* candidate : mDirectionalLightProxies)
        {
            uint32_t sunLightIndex = candidate->getAtmosphereSunLightIndex();
            if (!candidate->isVisible() ||
                (requireAffectAtmosphere && !candidate->isAffectAtmosphere()) ||
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
    }

    AtmosphereProxy* SceneProxy::getActiveAtmosphereProxy() const noexcept
    {
        for (AtmosphereProxy* proxy : mAtmosphereProxies)
        {
            if (proxy->isVisible())
                return proxy;
        }
        return nullptr;
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
        return selected;
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

    void SceneProxy::clearGSplatRenderProxySplatData(GSplatRenderProxy* gsplatRenderProxy)
    {
        if (!mGSplatScene)
            return;

        mGSplatScene->removeGSplat(gsplatRenderProxy->getGSplatId());
        if (mGSplatRenderProxies.empty())
            mGSplatScene = nullptr;
    }

    void SceneProxy::update()
    {
        // 唯一的创建入口: mLumenEnabled 是显式开关(见 setLumenEnabled 的注释)
        if (mLumenEnabled && !mLumenScene)
        {
            mLumenScene = std::make_unique<LumenScene>(getRenderBackend());
        }

        if (!mLumenScene)
            return;

        if (!mLumenEnabled)
            return;

        // card 分页/GDF cascade 的决策已经下放给 CardCapturePass/GlobalDistanceFieldCompositePass
        // 自己的 builder——这里只剩阴影候选剔选还没有专属 pass 可以挂, 暂时留在这里直接调用。
        // referenceOrigin 固定用第一个相机(跟 CardDirectLightingPass 点/聚光阴影固定用主相机
        // GDF 是同一个简化, 见 Lumen_GI_Roadmap.md), 没有相机时退化用世界原点——这种情况下
        // mCameraProxies 为空, updateDirectLightingShadowCandidates() 内部会直接整体跳过
        WorldCoord referenceOrigin;
        if (!mCameraProxies.empty())
        {
            referenceOrigin.fromDouble3(mCameraProxies[0]->getWorldPosition());
        }
        mLumenScene->updateDirectLightingShadowCandidates(mMeshRenderProxies, mCameraProxies, mDirectionalLightProxies, referenceOrigin);

        // 把每盏方向光刚算出来的 tile grid 在共享 buffer 里的偏移写回它的 LightData——
        // LumenScene 故意不持有 SceneProxy(只接收它需要的那几个 span), 这份"写回调用方
        // 的数据"的职责因此落在这里, 不是 LumenScene 自己的事
        for (DirectionalLightProxy* directionalLightProxy : mDirectionalLightProxies)
        {
            const LumenScene::DirectLightingShadowTileGrid* tileGrid =
                mLumenScene->getDirectLightingShadowTileGrid(directionalLightProxy);
            uint32_t lightIndex = directionalLightProxy->getLightIndex();
            uint32_t newOffset = tileGrid ? tileGrid->gpuBufferOffset : kInvalidSurfaceCacheAtlasPageIndex;
            if (getLightData(lightIndex).shadowTileGridOffset != newOffset)
            {
                getLightData(lightIndex).shadowTileGridOffset = newOffset;
                markLightDataUpdated(lightIndex);
            }
        }
    }

    SkinningManager& SceneProxy::getOrCreateSkinningManager()
    {
        if (!mSkinningManager)
        {
            mSkinningManager = std::make_unique<SkinningManager>(getRenderBackend(), kBaseSkinningMatrixCapacity);
            createSkeletalDrawBindGroup();
        }
        return *mSkinningManager;
    }

    void SceneProxy::createSkeletalDrawBindGroup()
    {
        auto& renderBackend = getRenderBackend();

        rhi::BufferBindingInfo instanceDataBinding;
        instanceDataBinding.buffer = mInstanceDataBuffer.get();
        instanceDataBinding.offset = 0;
        instanceDataBinding.range = kBaseInstanceCount * sizeof(InstanceData);

        rhi::BufferBindingInfo indicesBinding;
        indicesBinding.buffer = mIndicesBuffer;
        indicesBinding.offset = 0;
        indicesBinding.range = mIndicesBuffer->getDesc().size;

        rhi::BufferBindingInfo matrixBinding;
        matrixBinding.buffer = &mSkinningManager->getMatrixBuffer();
        matrixBinding.offset = 0;
        matrixBinding.range = mSkinningManager->getMatrixBuffer().getDesc().size;

        rhi::BufferBindingInfo meshInstanceDataBinding;
        meshInstanceDataBinding.buffer = mMeshInstanceDataBuffer.get();
        meshInstanceDataBinding.offset = 0;
        meshInstanceDataBinding.range = kBaseMeshInstanceCount * sizeof(MeshInstanceData);

        rhi::BindGroupEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].type = rhi::BindingType::Storage_Buffer;
        entries[0].buffers = { &instanceDataBinding, 1 };
        entries[1].binding = 1;
        entries[1].type = rhi::BindingType::Storage_Buffer;
        entries[1].buffers = { &indicesBinding, 1 };
        entries[2].binding = 2;
        entries[2].type = rhi::BindingType::Storage_Buffer;
        entries[2].buffers = { &meshInstanceDataBinding, 1 };
        entries[3].binding = 3;
        entries[3].type = rhi::BindingType::Storage_Buffer;
        entries[3].buffers = { &matrixBinding, 1 };

        mSkeletalDrawBindGroup = renderBackend.getDevice().createBindGroup(
            *RendererContext::createSkeletalDrawBindGroupLayout(renderBackend.getBindGroupLayoutCache())
        );
        mSkeletalDrawBindGroup->update({ entries, 4 });
    }

    QuadManager& SceneProxy::getOrCreateQuadManager()
    {
        if (!mQuadManager)
        {
            mQuadManager = std::make_unique<QuadManager>(getRenderBackend(), kBaseQuadElementCapacity, kBaseQuadInstanceCapacity);
            createQuadDrawBindGroup();
        }
        return *mQuadManager;
    }

    void SceneProxy::createQuadDrawBindGroup()
    {
        auto& renderBackend = getRenderBackend();

        rhi::BufferBindingInfo instanceDataBinding;
        instanceDataBinding.buffer = mInstanceDataBuffer.get();
        instanceDataBinding.offset = 0;
        instanceDataBinding.range = kBaseInstanceCount * sizeof(InstanceData);

        rhi::BufferBindingInfo indicesBinding;
        indicesBinding.buffer = mIndicesBuffer;
        indicesBinding.offset = 0;
        indicesBinding.range = mIndicesBuffer->getDesc().size;

        rhi::BufferBindingInfo elementBinding;
        elementBinding.buffer = &mQuadManager->getElementBuffer();
        elementBinding.offset = 0;
        elementBinding.range = mQuadManager->getElementBuffer().getDesc().size;

        rhi::BufferBindingInfo quadInstanceBinding;
        quadInstanceBinding.buffer = &mQuadManager->getInstanceDataBuffer();
        quadInstanceBinding.offset = 0;
        quadInstanceBinding.range = mQuadManager->getInstanceDataBuffer().getDesc().size;

        rhi::BindGroupEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].type = rhi::BindingType::Storage_Buffer;
        entries[0].buffers = { &instanceDataBinding, 1 };
        entries[1].binding = 1;
        entries[1].type = rhi::BindingType::Storage_Buffer;
        entries[1].buffers = { &indicesBinding, 1 };
        entries[2].binding = 2;
        entries[2].type = rhi::BindingType::Storage_Buffer;
        entries[2].buffers = { &elementBinding, 1 };
        entries[3].binding = 3;
        entries[3].type = rhi::BindingType::Storage_Buffer;
        entries[3].buffers = { &quadInstanceBinding, 1 };

        mQuadDrawBindGroup = renderBackend.getDevice().createBindGroup(
            *RendererContext::createQuadDrawBindGroupLayout(renderBackend.getBindGroupLayoutCache())
        );
        mQuadDrawBindGroup->update({ entries, 4 });
    }

    void SceneProxy::prepare(rhi::CommandList& cmdList)
    {
        if (mLumenScene)
        {
            SurfaceCacheAtlas& surfaceCacheAtlas = mLumenScene->getSurfaceCacheAtlas();
            surfaceCacheAtlas.prepare(cmdList);
            surfaceCacheAtlas.flushCardPageTableUploads(cmdList);
        }

        mSectionTable->prepare(cmdList);

        if (mSkinningManager)
        {
            mSkinningManager->prepare(cmdList);

            mSkinningBaseChanges.clear();
            mSkinningManager->takeBaseChanges(mSkinningBaseChanges);
            for (const SkinningManager::BaseChange& change : mSkinningBaseChanges)
            {
                MeshInstanceData& meshInstanceData = mMeshInstanceData[change.ownerId];
                meshInstanceData.boneMatrixRangeBase = change.currentBase;
                meshInstanceData.prevBoneMatrixRangeBase = change.prevBase;
                markMeshInstanceDataUpdated(change.ownerId);
            }
        }

        if (mQuadManager)
            mQuadManager->prepare(cmdList);

        auto& stagingAllocator = getRenderBackend().getStagingAllocator();

        // 上一帧发生过 Transform 变化、但本帧没有继续变化的实例，需要补写一次
        // prev=current，让其 Velocity 从本帧开始稳定回到 0。这里只标通用上传脏位，不能再标
        // Transform 脏位，否则该实例会每帧反复进入这个分支并永久上传。
        for (uint32_t wordIndex = 0; wordIndex < mPrevInstanceTransformUpdatedBitset.size(); ++wordIndex)
        {
            uint64_t resetWord = mPrevInstanceTransformUpdatedBitset[wordIndex]
                & ~mInstanceTransformUpdatedBitset[wordIndex];
            while (resetWord != 0)
            {
                uint32_t bitIndex = static_cast<uint32_t>(std::countr_zero(resetWord));
                uint32_t instanceIndex = wordIndex * 64 + bitIndex;
                auto& instanceData = mInstanceData[instanceIndex];
                instanceData.prevObjectToWorld = instanceData.objectToWorld;
                markInstanceDataUpdated(instanceIndex);

                resetWord &= resetWord - 1;
            }
        }

        struct TableUpload
        {
            std::vector<std::pair<size_t, size_t>> ranges;
            size_t elementSize;
            uint8_t* srcData;
            rhi::Buffer* dstBuffer;
        };

        TableUpload tableUploads[] = {
            { mInstanceData.generateUpdateRanges(), sizeof(InstanceData), reinterpret_cast<uint8_t*>(mInstanceData.data()), mInstanceDataBuffer.get() },
            { mLightData.generateUpdateRanges(), sizeof(LightData), reinterpret_cast<uint8_t*>(mLightData.data()), mLightDataBuffer.get() },
            { mMeshInstanceData.generateUpdateRanges(), sizeof(MeshInstanceData), reinterpret_cast<uint8_t*>(mMeshInstanceData.data()), mMeshInstanceDataBuffer.get() },
        };

        auto makeBarrier = [](rhi::Buffer* buffer, rhi::ResourceState oldState, rhi::ResourceState newState)
        {
            rhi::BufferBarrier barrier;
            barrier.buffer = buffer;
            barrier.offset = 0;
            barrier.size = rhi::kWholeBuffer;
            barrier.oldState = oldState;
            barrier.newState = newState;
            return barrier;
        };

        std::vector<rhi::BufferBarrier> preBarriers;
        for (const TableUpload& upload : tableUploads)
        {
            if (!upload.ranges.empty())
            {
                preBarriers.push_back(makeBarrier(upload.dstBuffer, rhi::ResourceState::Shader_Storage_Read, rhi::ResourceState::Transfer_Dst));
            }
        }

        if (!preBarriers.empty())
        {
            cmdList.insertBarriers(preBarriers);
        }

        std::vector<rhi::BufferBarrier> postBarriers;
        for (const TableUpload& upload : tableUploads)
        {
            if (!upload.ranges.empty())
            {
                uploadData(cmdList, upload.ranges, upload.elementSize, upload.srcData, upload.dstBuffer, stagingAllocator);
                postBarriers.push_back(makeBarrier(upload.dstBuffer, rhi::ResourceState::Transfer_Dst, rhi::ResourceState::Shader_Storage_Read));
            }
        }

        mInstanceData.clearUpdated();
        mLightData.clearUpdated();
        mMeshInstanceData.clearUpdated();

        if (!postBarriers.empty())
        {
            cmdList.insertBarriers(postBarriers);
        }

        // previous 只继承本帧的真实 Transform 变化；上面生成的 prev=current 修正只存在于
        // mInstanceData 的上传脏位中，因此不会被带到下一帧形成循环上传。
        mPrevInstanceTransformUpdatedBitset.swap(mInstanceTransformUpdatedBitset);
        std::fill(mInstanceTransformUpdatedBitset.begin(), mInstanceTransformUpdatedBitset.end(), 0);

        DirectionalLightProxy* mainLights[2];
        selectSunLights(false, mainLights);
        for (uint32_t i = 0; i < 2; ++i)
        {
            mMainLightDirections[i] = mainLights[i] ? -mainLights[i]->getDirection() : float3(0, 0, 1);
            mMainLightIntensities[i] = mainLights[i] ? mainLights[i]->getIntensity() : float3(0);
        }

        if (AtmosphereProxy* activeAtmosphere = getActiveAtmosphereProxy())
        {
            DirectionalLightProxy* sunLights[2];
            selectSunLights(true, sunLights);

            for (uint32_t i = 0; i < 2; ++i)
            {
                float3 direction = sunLights[i] ? -sunLights[i]->getDirection() : float3(0, 0, 1);
                float3 intensity = sunLights[i] ? sunLights[i]->getIntensity() : float3(0);

                activeAtmosphere->setSunLight(i, direction, intensity);
            }

            activeAtmosphere->prepare(cmdList);
        }

        if (SkyLightProxy* activeSkyLight = getActiveSkyLightProxy())
            activeSkyLight->prepare(cmdList);

        if (mGSplatScene)
        {
            mGSplatScene->prepare(cmdList);
        }
    }

    std::vector<SceneViewGroup> SceneProxy::gatherSceneViewGroups(ViewDataManager& viewDataManager, uint32_t frameIndex) const
    {
        std::vector<SceneViewGroup> sceneViewGroups;

        // extent 为 0 表示相机当前不输出(见 SceneViewport), 此时宽高比是 0/0, 投影矩阵会带 NaN
        std::vector<const CameraProxy*> activeCameraProxies;
        activeCameraProxies.reserve(mCameraProxies.size());
        for (CameraProxy* cameraProxy : mCameraProxies)
        {
            if (!cameraProxy->isViewportEmpty())
                activeCameraProxies.push_back(cameraProxy);
        }

        size_t expectedSceneViewCount = mDirectionalLightProxies.size() * activeCameraProxies.size() + activeCameraProxies.size();
        sceneViewGroups.reserve(expectedSceneViewCount);
        for (auto directionalLightProxy : mDirectionalLightProxies)
        {
            if (!directionalLightProxy->isVisible() || !directionalLightProxy->isCastShadows())
                continue;

            auto shadowSceneViewGroups = directionalLightProxy->getShadowSceneViewGroups(activeCameraProxies, viewDataManager);

            sceneViewGroups.insert(
                sceneViewGroups.end(),
                std::make_move_iterator(shadowSceneViewGroups.begin()),
                std::make_move_iterator(shadowSceneViewGroups.end())
            );
        }
        for (const CameraProxy* cameraProxy : activeCameraProxies)
        {
            sceneViewGroups.emplace_back(cameraProxy->getSceneViewGroup(frameIndex));
        }
        return sceneViewGroups;
    }

    uint32_t SceneProxy::acquireInstanceIndex()
    {
        uint32_t instanceIndex = mInstanceData.acquire();
        size_t bitsetWordCount = (mInstanceData.size() + 64 - 1) / 64;
        mInstanceTransformUpdatedBitset.resize(bitsetWordCount, 0);
        mPrevInstanceTransformUpdatedBitset.resize(bitsetWordCount, 0);
        return instanceIndex;
    }

    void SceneProxy::releaseInstanceIndex(uint32_t instanceIndex)
    {
        uint32_t wordIndex = instanceIndex / 64;
        uint64_t bit = 1ull << (instanceIndex % 64);
        assert(wordIndex < mInstanceTransformUpdatedBitset.size());
        mInstanceTransformUpdatedBitset[wordIndex] &= ~bit;
        mPrevInstanceTransformUpdatedBitset[wordIndex] &= ~bit;
        getInstanceData(instanceIndex).flags = InstanceFlags::None;
        markInstanceDataUpdated(instanceIndex);
        mInstanceData.release(instanceIndex);
    }

    uint32_t SceneProxy::acquireLightIndex()
    {
        return mLightData.acquire();
    }

    void SceneProxy::releaseLightIndex(uint32_t lightIndex)
    {
        mLightData.release(lightIndex);
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
