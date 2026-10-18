#include "RendererContext.h"
#include "SceneView.h"

namespace hyp
{
    namespace
    {
        rhi::BindGroupLayoutEntry makeReadOnlyStorageBufferEntry(uint32_t binding, rhi::ShaderStage stageFlags)
        {
            rhi::BindGroupLayoutEntry entry;
            entry.binding = binding;
            entry.type = rhi::BindingType::Storage_Buffer;
            entry.access = rhi::BindingAccess::Read_Only;
            entry.count = 1;
            entry.stageFlags = stageFlags;
            return entry;
        }
    }

    RendererContext::RendererContext(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend),
        mViewDataManager(renderBackend),
        mIndicesBuffer(renderBackend)
    {
        initGlobalPipelineLayout();

        auto& bindGroupLayoutCache = mRenderBackend.getBindGroupLayoutCache();
        mInstanceDataBindGroupLayout = createInstanceDataBindGroupLayout(bindGroupLayoutCache);
        mDrawBindGroupLayout = createDrawBindGroupLayout(bindGroupLayoutCache);
        mSkeletalDrawBindGroupLayout = createSkeletalDrawBindGroupLayout(bindGroupLayoutCache);
        mQuadDrawBindGroupLayout = createQuadDrawBindGroupLayout(bindGroupLayoutCache);
    }

    TRef<rhi::BindGroupLayout> RendererContext::createInstanceDataBindGroupLayout(BindGroupLayoutCache& cache)
    {
        const rhi::BindGroupLayoutEntry entries[] = {
            makeReadOnlyStorageBufferEntry(0, rhi::ShaderStage::All),
        };
        return cache.getOrCreate(entries);
    }

    TRef<rhi::BindGroupLayout> RendererContext::createDrawBindGroupLayout(BindGroupLayoutCache& cache)
    {
        const rhi::BindGroupLayoutEntry entries[] = {
            makeReadOnlyStorageBufferEntry(0, rhi::ShaderStage::All),
            makeReadOnlyStorageBufferEntry(1, rhi::ShaderStage::Vertex | rhi::ShaderStage::Compute),
            makeReadOnlyStorageBufferEntry(2, rhi::ShaderStage::All),
        };
        return cache.getOrCreate(entries);
    }

    TRef<rhi::BindGroupLayout> RendererContext::createSkeletalDrawBindGroupLayout(BindGroupLayoutCache& cache)
    {
        const rhi::BindGroupLayoutEntry entries[] = {
            makeReadOnlyStorageBufferEntry(0, rhi::ShaderStage::All),
            makeReadOnlyStorageBufferEntry(1, rhi::ShaderStage::Vertex | rhi::ShaderStage::Compute),
            makeReadOnlyStorageBufferEntry(2, rhi::ShaderStage::All),
            makeReadOnlyStorageBufferEntry(3, rhi::ShaderStage::All),
        };
        return cache.getOrCreate(entries);
    }

    TRef<rhi::BindGroupLayout> RendererContext::createQuadDrawBindGroupLayout(BindGroupLayoutCache& cache)
    {
        const rhi::BindGroupLayoutEntry entries[] = {
            makeReadOnlyStorageBufferEntry(0, rhi::ShaderStage::All),
            makeReadOnlyStorageBufferEntry(1, rhi::ShaderStage::Vertex | rhi::ShaderStage::Compute),
            makeReadOnlyStorageBufferEntry(2, rhi::ShaderStage::All),
            makeReadOnlyStorageBufferEntry(3, rhi::ShaderStage::All),
        };
        return cache.getOrCreate(entries);
    }

    void RendererContext::initGlobalPipelineLayout()
    {
        PipelineCache::PipelineLayoutKey pipelineLayoutKey;

        auto& globalBindings = mRenderBackend.getGlobalBindings();
        mGlobalDataBindGroupLayout = &globalBindings.getGlobalDataBindGroup().getLayout();
        pipelineLayoutKey.bindGroupLayouts[0] = mGlobalDataBindGroupLayout;

        pipelineLayoutKey.bindGroupLayouts[1] = &mViewDataManager.getBindGroupLayout();

        pipelineLayoutKey.bindGroupLayouts[2] = nullptr;
        pipelineLayoutKey.bindGroupLayouts[3] = nullptr;

        mGlobalPipelineLayout = mRenderBackend.getPipelineCache().getOrCreateLayout(pipelineLayoutKey);
    }

}
