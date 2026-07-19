#pragma once
#include "SceneView.h"
#include "PassStorage.h"

#include <RenderBase/RenderBackend.h>

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

        const rhi::PipelineLayout& getGlobalPipelineLayout() const noexcept
        {
            return *mGlobalPipelineLayout;
        }

        const rhi::BindGroupLayout& getGlobalDataBindGroupLayout() const noexcept
        {
            return *mGlobalDataBindGroupLayout;
        }

        const rhi::BindGroupLayout& getSceneAndViewBindGroupLayout() const noexcept
        {
            return *mSceneAndViewBindGroupLayout;
        }

    private:
        void initGlobalPipelineLayout();

    private:
        RenderBackend& mRenderBackend;
        TRef<const rhi::BindGroupLayout> mGlobalDataBindGroupLayout;
        TRef<const rhi::BindGroupLayout> mSceneAndViewBindGroupLayout;
        TRef<const rhi::PipelineLayout> mGlobalPipelineLayout;

        ViewDataManager mViewDataManager;
        PassStorage mPassStorage;
    };
}
