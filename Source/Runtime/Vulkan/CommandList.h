#pragma once
#include "Device.h"

#include <RHI/CommandList.h>

namespace hyp::vk
{
    class CommandList : public rhi::CommandList
    {
    public:
        CommandList(const Device& device, rhi::QueueType queueType, VkCommandPool commandPool);

        ~CommandList();

        void reset() override;

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkCommandBuffer getHandle() const noexcept
        {
            return mCommandBuffer;
        }

        void decode();

#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
#define HYP_RHI_DECLARE_BACKEND_OVERRIDE(ret, name, params) \
        ret name##Backend params override;
        HYP_RHI_COMMAND_LIST_METHODS(HYP_RHI_DECLARE_BACKEND_OVERRIDE)
#undef HYP_RHI_DECLARE_BACKEND_OVERRIDE
#else
#define HYP_RHI_DECLARE_BACKEND(ret, name, params) \
        ret name##Backend params;
        HYP_RHI_COMMAND_LIST_METHODS(HYP_RHI_DECLARE_BACKEND)
#undef HYP_RHI_DECLARE_BACKEND
#endif

    private:
        VkCommandPool mCommandPool = VK_NULL_HANDLE;
        VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;

        bool mDynamicRendering = false;
        bool mSynchronization2 = false;
    };
}
