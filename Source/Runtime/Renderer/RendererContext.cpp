#include "RendererContext.h"
#include "SceneProxy.h"
#include "SceneView.h"

namespace hyp
{
    RendererContext::RendererContext(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend),
        mViewDataManager(renderBackend)
    {
        initGlobalPipelineLayout();
    }

    void RendererContext::initGlobalPipelineLayout()
    {
        PipelineCache::PipelineLayoutKey pipelineLayoutKey;

        auto& globalBindings = mRenderBackend.getGlobalBindings();
        mGlobalDataBindGroupLayout = &globalBindings.getGlobalDataBindGroup().getLayout();
        pipelineLayoutKey.bindGroupLayouts[0] = mGlobalDataBindGroupLayout;

        mSceneAndViewBindGroupLayout = SceneProxy::createSceneAndViewBindGroupLayout(mRenderBackend.getBindGroupLayoutCache());
        pipelineLayoutKey.bindGroupLayouts[1] = mSceneAndViewBindGroupLayout;

        pipelineLayoutKey.bindGroupLayouts[2] = nullptr;
        pipelineLayoutKey.bindGroupLayouts[3] = nullptr;

        mGlobalPipelineLayout = mRenderBackend.getPipelineCache().getOrCreate(pipelineLayoutKey);
    }

}
