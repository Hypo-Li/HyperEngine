#include "SceneView.h"

namespace hyp
{
    const HString gDefaultRTName = "DefaultRT";

    SceneView::SceneView(
        uint32_t viewId,
        uint32_t cullingMask,
        uint32_t renderMask,
        const double4x4& viewMatrix,
        const float4x4& projectionMatrix,
        float4 viewport,
        HString finalRTName,
        OutputRouting outputRouting
    ) : mViewId(viewId),
        mCullingMask(cullingMask),
        mRenderMask(renderMask),
        mWorldToViewRot(float3x4(viewMatrix)),
        mProjectionMatrix(projectionMatrix),
        mFrustum(double4x4(projectionMatrix) * viewMatrix),
        mViewport(viewport),
        mFinalRTName(finalRTName),
        mOutputRouting(outputRouting)
    {
        double3 translation = viewMatrix[3];
        double3 r0 = viewMatrix[0];
        double3 r1 = viewMatrix[1];
        double3 r2 = viewMatrix[2];

        mCameraOrigin.x = -Math::dot(r0, translation);
        mCameraOrigin.y = -Math::dot(r1, translation);
        mCameraOrigin.z = -Math::dot(r2, translation);
    }

    ViewData SceneView::makeViewData() const noexcept
    {
        ViewData viewData;
        viewData.worldToViewRot = mWorldToViewRot;
        viewData.cameraOrigin.fromDouble3(mCameraOrigin);
        viewData.viewToClip = mProjectionMatrix;
        viewData.clipToView = glm::inverse(mProjectionMatrix);
        viewData.viewport = mViewport;
        viewData.resolution = float4(mViewport.z, mViewport.w, 1.0f / mViewport.z, 1.0f / mViewport.w);
        return viewData;
    }

    ViewDataManager::ViewDataManager(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend)
    {
        auto& device = renderBackend.getDevice();
        mViewDataAlignedSize = static_cast<uint32_t>((sizeof(ViewData) + kStorageBufferOffsetAlignment - 1) & ~(kStorageBufferOffsetAlignment - 1));

        rhi::BufferDesc bufferDesc;
        bufferDesc.size = mViewDataAlignedSize * kMaxViews;
        bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
        mViewDataBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
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
        rhi::CommandList& cmd,
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
        cmd.copyBuffer(allocation.buffer, mViewDataBuffer.get(), copyRegions);

        rhi::BufferBarrier barrier;
        barrier.buffer = mViewDataBuffer.get();
        barrier.offset = 0;
        barrier.size = rhi::kWholeBuffer;
        barrier.oldState = rhi::ResourceState::Transfer_Dst;
        barrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmd.insertBarriers({ &barrier, 1 });
    }

}
