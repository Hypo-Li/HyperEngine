#pragma once
#include "Buffer.h"
#include "Texture.h"
#include "Sampler.h"
#include "BindGroup.h"
#include "ShaderModule.h"
#include "Pipeline.h"
#include "CommandList.h"
#include "Fence.h"
#include "Swapchain.h"
#include "Features.h"

namespace hyp::rhi
{
    enum class SubmitFlags : uint32_t
    {
        None = 0,
        Wait_Swapchain_Image = 1 << 0,  // 等待 swapchain image 可用
        Signal_Present_Ready = 1 << 1,  // 标记 swapchain image 可提交 present
    };
    ENABLE_ENUM_CLASS_BITWISE(SubmitFlags)

    class Context;
    class Device
    {
    public:
        Device() = default;

        virtual ~Device() = default;

        Swapchain* getSwapchain() const noexcept
        {
            return mSwapchain.get();
        }

        const FeatureSupport& getFeatures() const noexcept
        {
            return mFeatures;
        }

        const ShaderCompileTarget& getShaderCompileTarget() const noexcept
        {
            return mShaderCompileTarget;
        }

        virtual void waitIdle() const = 0;

        // 派生类实现 create{Buffer,Texture,Sampler}Impl(), 这里统一打一个 unique id 再返回——
        // BindGroupCache/TransientResources 这类靠"资源是不是变了"做增量更新判断的代码, 不能只
        // 比裸指针: 对象销毁后, 分配器可能把下一个新对象分配到同一个地址上, 裸指针会把"新建的
        // 资源"误判成"还是原来那个没变过"。调用方(现有的 device.createBuffer()/createTexture()/
        // createSampler() 调用点)不需要改, 签名和行为完全不变, 只是内部多打了一个 ID。
        TRef<Buffer> createBuffer(const BufferDesc& desc, MemoryType memoryType) const
        {
            TRef<Buffer> buffer = createBufferImpl(desc, memoryType);
            buffer->setUniqueId(nextUniqueId());
            return buffer;
        }

        TRef<Texture> createTexture(const TextureDesc& desc, MemoryType memoryType) const
        {
            TRef<Texture> texture = createTextureImpl(desc, memoryType);
            texture->setUniqueId(nextUniqueId());
            return texture;
        }

        TRef<Sampler> createSampler(const SamplerDesc& desc) const
        {
            TRef<Sampler> sampler = createSamplerImpl(desc);
            sampler->setUniqueId(nextUniqueId());
            return sampler;
        }

        virtual TRef<BindGroupLayout> createBindGroupLayout(std::span<const BindGroupLayoutEntry> entries) const = 0;

        virtual TRef<BindGroup> createBindGroup(const BindGroupLayout& layout) const = 0;

        virtual TRef<ShaderModule> createShaderModule(ShaderStage stage, std::vector<uint8_t>&& code, ShaderCodeFormat codeFormat) const = 0;

        virtual TRef<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) const = 0;

        virtual TRef<Pipeline> createGraphicsPipeline(const GraphicsPipelineDesc& desc) const = 0;

        virtual TRef<Pipeline> createComputePipeline(const ComputePipelineDesc& desc) const = 0;

        virtual TRef<Fence> createFence() const = 0;

        virtual TRef<CommandList> createCommandList(QueueType queueType) const = 0;

        virtual void submit(
            CommandList* commandList,
            Fence* waitFence,
            uint64_t waitValue,
            Fence* signalFence,
            uint64_t signalValue,
            SubmitFlags flags = SubmitFlags::None
        ) const = 0;

    protected:
        virtual TRef<Buffer> createBufferImpl(const BufferDesc& desc, MemoryType memoryType) const = 0;

        virtual TRef<Texture> createTextureImpl(const TextureDesc& desc, MemoryType memoryType) const = 0;

        virtual TRef<Sampler> createSamplerImpl(const SamplerDesc& desc) const = 0;

    protected:
        std::unique_ptr<Swapchain> mSwapchain;
        FeatureSupport mFeatures;
        ShaderCompileTarget mShaderCompileTarget;

    private:
        static uint64_t nextUniqueId() noexcept
        {
            static uint64_t counter = 1;
            return counter++;
        }
    };
}
