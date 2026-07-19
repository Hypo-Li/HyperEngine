#pragma once
#include "Device.h"

#include <RHI/Texture.h>

namespace hyp::wgpu
{
    class Texture;

    class TextureView : public rhi::TextureView
    {
    public:
        TextureView(Texture& texture, const rhi::TextureViewDesc& desc);

        ~TextureView() override;

        const Texture& getWgpuTexture() const noexcept;

        WGPUTextureView getHandle() const noexcept
        {
            return mTextureView;
        }

        void setDebugName(HString name) override;

    private:
        WGPUTextureView mTextureView = nullptr;
    };

    class Texture : public rhi::Texture
    {
    public:
        Texture(const Device& device, const rhi::TextureDesc& desc, rhi::MemoryType memoryType);

        Texture(const Device& device, const rhi::TextureDesc& desc, rhi::MemoryType memoryType, WGPUTexture handle);

        ~Texture() override;

        Texture& operator=(WGPUTexture newHandle) noexcept
        {
            assert(!mOwnsHandle);
            mTexture = newHandle;
            return *this;
        }

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        WGPUTexture getHandle() const noexcept
        {
            return mTexture;
        }

        TRef<rhi::TextureView> createView(const rhi::TextureViewDesc& desc) override;

        void setDebugName(HString name) override;

    private:
        WGPUTexture mTexture = nullptr;
        bool mOwnsHandle = true;
    };
}
