#pragma once
#include "Device.h"

#include <RHI/BindGroup.h>

#include <dawn/webgpu.h>

namespace hyp::wgpu
{
    class BindGroupLayout : public rhi::BindGroupLayout
    {
    public:
        BindGroupLayout(const Device& device, std::span<const rhi::BindGroupLayoutEntry> entries);

        ~BindGroupLayout() override;

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        WGPUBindGroupLayout getHandle() const noexcept
        {
            return mLayout;
        }

        uint32_t getDynamicOffsetCount() const noexcept
        {
            return mDynamicOffsetCount;
        }

    private:
        WGPUBindGroupLayout mLayout = nullptr;
        uint32_t mDynamicOffsetCount = 0;
    };

    class BindGroup : public rhi::BindGroup
    {
    public:
        BindGroup(const Device& device, const BindGroupLayout& layout);

        ~BindGroup() override;

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        const BindGroupLayout& getWgpuLayout() const noexcept
        {
            return mWgpuLayout;
        }

        WGPUBindGroup getHandle() const noexcept
        {
            return mBindGroup;
        }

        void update(std::span<const rhi::BindGroupEntry> entries) override;

    private:
        void createWgpuBindGroup(std::span<const rhi::BindGroupEntry> entries);

        const BindGroupLayout& mWgpuLayout;
        WGPUBindGroup mBindGroup = nullptr;
    };
}
