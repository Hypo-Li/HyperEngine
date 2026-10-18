#include "SceneView.h"

namespace hyp
{
    const HString gDefaultRTName = "DefaultRT";

    namespace
    {
        // view matrix 是从世界到 view 空间的刚体变换的逆(R^T, -R^T*t 那种形式), 相机在世界空间的
        // 原点可以直接从它的行/列反解出来, 不需要真的求一次逆矩阵——CameraProxy/CSM 都用这个
        // 提取当前帧和上一帧的相机原点, 提出来避免两处各写一遍。
        double3 extractCameraOrigin(const double4x4& viewMatrix) noexcept
        {
            double3 translation = viewMatrix[3];
            double3 r0 = viewMatrix[0];
            double3 r1 = viewMatrix[1];
            double3 r2 = viewMatrix[2];
            return double3(
                -Math::dot(r0, translation),
                -Math::dot(r1, translation),
                -Math::dot(r2, translation)
            );
        }

        // 反解出不带 jitter 的投影矩阵(ViewData::viewToClip, 给 TAA motion vector 用)。CameraProxy::
        // getSceneViewGroup() 往矩阵里叠 jitter 时只改了两个分量(见那边的注释): 透视改 M[2][0]/
        // M[2][1], 正交改 M[3][0]/M[3][1]——而这两个分量在"干净"的矩阵里(makePerspectiveMatrix/
        // makeOrthographicMatrix)本来就是 0, jitter 只是往上面叠了一个偏移量, 不是在某个非零值上
        // 累加, 所以不需要知道 jitterOffset 具体是多少、也不需要关心叠加时用的是加还是减, 直接把
        // 这两个分量清零就是原来的值。用 M[2][3] 判断透视/正交(透视恒为 -1, 正交恒为 0, jitter
        // 从来不碰这个分量), 不需要额外传 ProjectionType。
        float4x4 unjitterProjectionMatrix(const float4x4& jitteredProjectionMatrix) noexcept
        {
            float4x4 result = jitteredProjectionMatrix;
            if (jitteredProjectionMatrix[2][3] != 0.0f)
            {
                result[2][0] = 0.0f;
                result[2][1] = 0.0f;
            }
            else
            {
                result[3][0] = 0.0f;
                result[3][1] = 0.0f;
            }
            return result;
        }
    }

    SceneView::SceneView(
        uint32_t viewId,
        uint32_t cullingMask,
        uint32_t renderMask,
        const double4x4& viewMatrix,
        const float4x4& projectionMatrix,
        float4 viewport,
        HString finalRTName,
        OutputRouting outputRouting,
        float2 jitterOffset,
        const std::optional<double4x4>& prevViewMatrix,
        const std::optional<float4x4>& prevProjectionMatrix,
        float preExposure,
        float prevPreExposure
    ) : mViewId(viewId),
        mCullingMask(cullingMask),
        mRenderMask(renderMask),
        mWorldToViewRot(float3x4(viewMatrix)),
        mProjectionMatrix(projectionMatrix),
        mFrustum(double4x4(projectionMatrix) * viewMatrix),
        mViewport(viewport),
        mFinalRTName(finalRTName),
        mOutputRouting(outputRouting),
        mJitterOffset(jitterOffset),
        mPrevWorldToViewRot(float3x4(prevViewMatrix.value_or(viewMatrix))),
        mPrevProjectionMatrix(prevProjectionMatrix.value_or(projectionMatrix)),
        mPreExposure(preExposure),
        mPrevPreExposure(prevPreExposure),
        mViewToClip(unjitterProjectionMatrix(projectionMatrix))
    {
        mCameraOrigin = extractCameraOrigin(viewMatrix);
        mPrevCameraOrigin = extractCameraOrigin(prevViewMatrix.value_or(viewMatrix));
    }

    SceneView::SceneView(
        uint32_t viewId,
        uint32_t cullingMask,
        uint32_t renderMask,
        const double4x4& viewMatrix,
        const float4x4& projectionMatrix,
        const Frustumd& cullingFrustum,
        float4 viewport,
        HString finalRTName,
        OutputRouting outputRouting
    ) : SceneView(
        viewId,
        cullingMask,
        renderMask,
        viewMatrix,
        projectionMatrix,
        viewport,
        finalRTName,
        outputRouting
    )
    {
        mFrustum = cullingFrustum;
    }

    ViewData SceneView::makeViewData() const noexcept
    {
        ViewData viewData;
        viewData.worldToViewRot = mWorldToViewRot;
        viewData.cameraOrigin.fromDouble3(mCameraOrigin);
        viewData.viewToClip = mProjectionMatrix;
        viewData.clipToView = glm::inverse(mProjectionMatrix);
        viewData.viewToClipUnjittered = mViewToClip;
        viewData.clipToViewUnjittered = glm::inverse(mViewToClip);
        viewData.viewport = mViewport;
        viewData.resolution = float4(mViewport.z, mViewport.w, 1.0f / mViewport.z, 1.0f / mViewport.w);
        viewData.jitterOffset = float4(mJitterOffset, 0.0f, 0.0f);
        viewData.prevWorldToViewRot = mPrevWorldToViewRot;
        viewData.prevCameraOrigin.fromDouble3(mPrevCameraOrigin);
        viewData.prevViewToClipUnjittered = mPrevProjectionMatrix;
        viewData.prevClipToViewUnjittered = glm::inverse(mPrevProjectionMatrix);
        viewData.preExposure = mPreExposure;
        viewData.oneOverPreExposure = 1.0f / mPreExposure;
        viewData.prevPreExposure = mPrevPreExposure;
        viewData.padding0 = 0.0f;
        return viewData;
    }

    SceneViewGroup::SceneViewGroup(Type type, std::vector<SceneView>&& sceneViews, SceneViewGroup::ViewSource viewSource) :
        mType(type),
        mSceneViews(std::move(sceneViews)),
        mViewSource(viewSource)
    {
    }

    SceneViewGroup::SceneViewGroup(SceneViewGroup&& other) noexcept :
        mType(other.mType),
        mSceneViews(std::move(other.mSceneViews)),
        mViewSource(other.mViewSource)
    {
    }

    SceneViewGroup& SceneViewGroup::operator=(SceneViewGroup&& other) noexcept
    {
        std::swap(mType, other.mType);
        std::swap(mSceneViews, other.mSceneViews);
        std::swap(mViewSource, other.mViewSource);
        return *this;
    }

    ViewDataManager::ViewDataManager(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend)
    {
        auto& device = renderBackend.getDevice();
        mViewDataAlignedSize = static_cast<uint32_t>((sizeof(ViewData) + kDynamicBufferOffsetAlignment - 1) & ~(kDynamicBufferOffsetAlignment - 1));

        rhi::BufferDesc bufferDesc;
        bufferDesc.size = mViewDataAlignedSize * kMaxViews;
        bufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Transfer_Dst;
        mViewDataBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);

        mBindGroupLayout = createBindGroupLayout(renderBackend.getBindGroupLayoutCache());
        mBindGroup = device.createBindGroup(*mBindGroupLayout);

        rhi::BufferBindingInfo viewDataBinding;
        viewDataBinding.buffer = mViewDataBuffer.get();
        viewDataBinding.offset = 0;
        viewDataBinding.range = sizeof(ViewData);

        rhi::BindGroupEntry entry = {};
        entry.binding = 0;
        entry.type = rhi::BindingType::Dynamic_Uniform_Buffer;
        entry.buffers = { &viewDataBinding, 1 };
        mBindGroup->update({ &entry, 1 });
    }

    TRef<rhi::BindGroupLayout> ViewDataManager::createBindGroupLayout(BindGroupLayoutCache& cache)
    {
        rhi::BindGroupLayoutEntry viewDataEntry;
        viewDataEntry.binding = 0;
        viewDataEntry.type = rhi::BindingType::Dynamic_Uniform_Buffer;
        viewDataEntry.access = rhi::BindingAccess::Read_Only;
        viewDataEntry.count = 1;
        viewDataEntry.stageFlags = rhi::ShaderStage::All;

        return cache.getOrCreate({ &viewDataEntry, 1 });
    }

    uint32_t ViewDataManager::acquireViewId()
    {
        if (!mFreeViewIds.empty())
        {
            uint32_t viewId = mFreeViewIds.back();
            mFreeViewIds.pop_back();
            return viewId;
        }

        uint32_t viewId = mNextViewId++;
        assert(viewId < kMaxViews);
        return viewId;
    }

    void ViewDataManager::releaseViewId(uint32_t viewId)
    {
        mFreeViewIds.push_back(viewId);
    }

    void ViewDataManager::updateViewData(
        rhi::CommandList& cmdList,
        std::span<const uint32_t> viewIds,
        std::span<const ViewData> viewData
    )
    {
        size_t viewCount = viewIds.size();
        if (!viewCount)
            return;
        auto allocation = mRenderBackend.getStagingAllocator().forceAllocate(viewCount * sizeof(ViewData));

        std::vector<rhi::BufferCopyRegion> copyRegions(viewCount);
        for (size_t i = 0; i < viewCount; ++i)
        {
            std::memcpy(
                static_cast<uint8_t*>(allocation.mappedData) + i * sizeof(ViewData),
                &viewData[i],
                sizeof(ViewData)
            );
            auto& copyRegion = copyRegions[i];
            copyRegion.srcOffset = allocation.offset + i * sizeof(ViewData);
            copyRegion.dstOffset = viewIds[i] * mViewDataAlignedSize;
            copyRegion.size = sizeof(ViewData);
        }
        cmdList.copyBuffer(allocation.buffer, mViewDataBuffer.get(), copyRegions);

        rhi::BufferBarrier barrier;
        barrier.buffer = mViewDataBuffer.get();
        barrier.offset = 0;
        barrier.size = rhi::kWholeBuffer;
        barrier.oldState = rhi::ResourceState::Transfer_Dst;
        barrier.newState = rhi::ResourceState::Shader_Uniform;
        cmdList.insertBarriers({ &barrier, 1 });
    }

}
