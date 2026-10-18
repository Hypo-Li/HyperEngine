#pragma once
#include <RHI/BindGroup.h>
#include <RHI/CommandList.h>
#include <RHI/Pipeline.h>
#include <RHI/Texture.h>

#include <memory>
#include <unordered_map>

namespace hyp
{
    class PassShader;
    class RenderBackend;

    class TextureClearer
    {
    public:
        explicit TextureClearer(RenderBackend& renderBackend);

        ~TextureClearer();

        // 占用 Compute 绑定点的 set 0, 返回后该绑定点的全部 set 失效, 由调用方恢复
        void clearStorageTexture(rhi::CommandList& cmdList, const rhi::TextureView& view, rhi::ResourceState oldState, rhi::ResourceState newState);

        void clearAttachmentTexture(rhi::CommandList& cmdList, const rhi::TextureView& view, rhi::ResourceState oldState, rhi::ResourceState newState);

    private:
        struct StorageClearPipeline
        {
            std::unique_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
        };

        StorageClearPipeline& getStorageClearPipeline(rhi::TextureFormat format);

    private:
        RenderBackend& mRenderBackend;
        std::unordered_map<rhi::TextureFormat, StorageClearPipeline> mStorageClearPipelines;
    };
}
