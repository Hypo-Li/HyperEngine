#pragma once
#include "RenderBaseDefines.h"
#include "BindlessRegistry.h"

#include <RHI/Buffer.h>
#include <RHI/Texture.h>
#include <RHI/Sampler.h>
#include <RHI/BindGroup.h>
#include <RHI/Pipeline.h>
#include <RHI/CommandList.h>

namespace hyp
{
    struct FrameData
    {
        float wrappedTime;
        uint32_t timeWrapCount;
        float deltaTime;
        uint32_t frameIndex;
        float prevWrappedTime;
        uint32_t prevTimeWrapCount;
        uint32_t padding0;
        uint32_t padding1;
    };

    class RenderBackend;
    class GlobalBindings
    {
    public:
        GlobalBindings(RenderBackend& renderBackend);

        const rhi::BindGroup& getGlobalDataBindGroup() const noexcept
        {
            return *mGlobalDataBindGroup;
        }

        const rhi::PipelineLayout& getPipelineLayout() const noexcept
        {
            return *mPipelineLayout;
        }

        void writeTexture2D(uint32_t index, rhi::TextureView* view);

        void writeTexture2DArray(uint32_t index, rhi::TextureView* view);

        void writeTexture3D(uint32_t index, rhi::TextureView* view);

        void writeTextureCube(uint32_t index, rhi::TextureView* view);

        void writeSampler(uint32_t index, rhi::Sampler* sampler);

        void updateFrameData(rhi::CommandList& cmdList, const FrameData& frameData);

    private:
        void initBindlessDefaultResources();

        void initGlobalDataBindGroup(const BindlessConfig& bindlessConfig);

    private:
        RenderBackend& mRenderBackend;

        bool mBindlessEnabled = false;

        TRef<rhi::Texture> mDefaultTexture2D;
        TRef<rhi::TextureView> mDefaultTexture2DView;
        TRef<rhi::TextureView> mDefaultTexture2DArrayView;

        TRef<rhi::Texture> mDefaultTexture3D;
        TRef<rhi::TextureView> mDefaultTexture3DView;

        TRef<rhi::Texture> mDefaultTextureCube;
        TRef<rhi::TextureView> mDefaultTextureCubeView;

        TRef<rhi::Sampler> mDefaultSampler;

        TRef<rhi::Buffer> mFrameDataUniformBuffer;

        TRef<rhi::BindGroupLayout> mGlobalDataBindGroupLayout;
        TRef<rhi::BindGroup> mGlobalDataBindGroup;
        TRef<rhi::PipelineLayout> mPipelineLayout;
    };
}
