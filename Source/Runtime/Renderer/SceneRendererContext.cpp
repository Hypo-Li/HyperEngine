#include "SceneRendererContext.h"
#include "SceneProxy.h"
#include "SceneView.h"

namespace hyp
{
    SceneRendererContext::SceneRendererContext(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend),
        mViewDataManager(renderBackend)
    {
        initGlobalPipelineLayout();
    }

    void SceneRendererContext::initGlobalPipelineLayout()
    {
        PipelineCache::PipelineLayoutKey pipelineLayoutKey;

        // Global Data
        auto& globalBindings = mRenderBackend.getGlobalBindings();
        mGlobalDataDescriptorSetLayout = &globalBindings.getGlobalDataDescriptorSet().getLayout();
        pipelineLayoutKey.descriptorSetLayouts[0] = mGlobalDataDescriptorSetLayout->getHandle();

        auto& device = mRenderBackend.getDevice();

        // Scene And View
        mSceneAndViewDescriptorSetLayout = SceneProxy::createSceneAndViewDescriptorSetLayout(device);
        pipelineLayoutKey.descriptorSetLayouts[1] = mSceneAndViewDescriptorSetLayout->getHandle();

        // Material / Pass Parameters
        pipelineLayoutKey.descriptorSetLayouts[2] = VK_NULL_HANDLE;

        // Object Data
        pipelineLayoutKey.descriptorSetLayouts[3] = VK_NULL_HANDLE;

        pipelineLayoutKey.pushConstantRangeSize = 128;

        mGlobalPipelineLayout = mRenderBackend.getPipelineCache().getOrCreatePipelineLayout(pipelineLayoutKey);
    }

}
