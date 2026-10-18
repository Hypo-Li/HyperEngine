#pragma once
#include "IndicesBuffer.h"
#include "SceneView.h"
#include "PassStorage.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/VertexLayout.h>

namespace hyp
{
    class RendererContext
    {
    public:
        RendererContext(RenderBackend& renderBackend);

        ~RendererContext() = default;

        RenderBackend& getRenderBackend() noexcept
        {
            return mRenderBackend;
        }

        ViewDataManager& getViewDataManager() noexcept
        {
            return mViewDataManager;
        }

        PassStorage& getPassStorage() noexcept
        {
            return mPassStorage;
        }

        IndicesBuffer& getIndicesBuffer() noexcept
        {
            return mIndicesBuffer;
        }

        const rhi::PipelineLayout& getGlobalPipelineLayout() const noexcept
        {
            return *mGlobalPipelineLayout;
        }

        const rhi::BindGroupLayout& getGlobalDataBindGroupLayout() const noexcept
        {
            return *mGlobalDataBindGroupLayout;
        }

        static TRef<rhi::BindGroupLayout> createInstanceDataBindGroupLayout(BindGroupLayoutCache& cache);

        static TRef<rhi::BindGroupLayout> createDrawBindGroupLayout(BindGroupLayoutCache& cache);

        static TRef<rhi::BindGroupLayout> createSkeletalDrawBindGroupLayout(BindGroupLayoutCache& cache);

        static TRef<rhi::BindGroupLayout> createQuadDrawBindGroupLayout(BindGroupLayoutCache& cache);

        const rhi::BindGroupLayout& getInstanceDataBindGroupLayout() const noexcept
        {
            return *mInstanceDataBindGroupLayout;
        }

        const rhi::BindGroupLayout& getDrawBindGroupLayout() const noexcept
        {
            return *mDrawBindGroupLayout;
        }

        const rhi::BindGroupLayout& getSkeletalDrawBindGroupLayout() const noexcept
        {
            return *mSkeletalDrawBindGroupLayout;
        }

        const rhi::BindGroupLayout& getQuadDrawBindGroupLayout() const noexcept
        {
            return *mQuadDrawBindGroupLayout;
        }

        const rhi::BindGroupLayout& getDrawBindGroupLayout(VertexLayoutType vertexLayoutType) const noexcept
        {
            switch (vertexLayoutType)
            {
            case VertexLayoutType::Skeletal_Mesh: return *mSkeletalDrawBindGroupLayout;
            case VertexLayoutType::Quad: return *mQuadDrawBindGroupLayout;
            default: return *mDrawBindGroupLayout;
            }
        }

    private:
        void initGlobalPipelineLayout();

    private:
        RenderBackend& mRenderBackend;
        TRef<const rhi::BindGroupLayout> mGlobalDataBindGroupLayout;
        TRef<const rhi::BindGroupLayout> mInstanceDataBindGroupLayout;
        TRef<const rhi::BindGroupLayout> mDrawBindGroupLayout;
        TRef<const rhi::BindGroupLayout> mSkeletalDrawBindGroupLayout;
        TRef<const rhi::BindGroupLayout> mQuadDrawBindGroupLayout;
        TRef<const rhi::PipelineLayout> mGlobalPipelineLayout;

        ViewDataManager mViewDataManager;
        IndicesBuffer mIndicesBuffer;
        PassStorage mPassStorage;
    };
}
