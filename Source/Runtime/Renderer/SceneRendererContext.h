#pragma once
#include "SceneView.h"
#include "PassStorage.h"

#include <RenderBase/RenderBackend.h>

namespace hyp
{
    class SceneRendererContext
    {
    public:
        SceneRendererContext(RenderBackend& renderBackend);

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

        const vk::PipelineLayout& getGlobalPipelineLayout() const noexcept
        {
            return *mGlobalPipelineLayout;
        }

        const vk::DescriptorSetLayout& getGlobalDataDescriptorSetLayout() const noexcept
        {
            return *mGlobalDataDescriptorSetLayout;
        }

        const vk::DescriptorSetLayout& getSceneAndViewDescriptorSetLayout() const noexcept
        {
            return *mSceneAndViewDescriptorSetLayout;
        }

    private:
        void initGlobalPipelineLayout();

    private:
        RenderBackend& mRenderBackend;
        ViewDataManager mViewDataManager;
        PassStorage mPassStorage;

        TRef<vk::PipelineLayout> mGlobalPipelineLayout;
        TRef<vk::DescriptorSetLayout> mGlobalDataDescriptorSetLayout;
        TRef<vk::DescriptorSetLayout> mSceneAndViewDescriptorSetLayout;
    };
}
