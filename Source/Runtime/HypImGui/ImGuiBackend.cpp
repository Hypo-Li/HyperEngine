#include "ImGuiBackend.h"

#include <Core/Common/LogSystem.h>

#include <Vulkan/Device.h>
#include <Vulkan/VkFormatUtil.h>
#include <WebGPU/Device.h>
#include <WebGPU/WgpuFormatUtil.h>

#include <RenderBase/RenderBaseDefines.h>

#include <backend/imgui_impl_vulkan.h>
#include <backend/imgui_impl_wgpu.h>

namespace hyp
{
    namespace
    {
        rhi::Backend gBackend = rhi::Backend::Vulkan;
        bool gAvailable = false;
        // ImGui_ImplVulkan_InitInfo::PipelineInfoMain.PipelineRenderingCreateInfo.
        // pColorAttachmentFormats 要求指向的内存在 backend 生命周期内保持有效, 不能用
        // initRenderer() 调用里的局部变量。
        VkFormat gVulkanColorFormatStorage = VK_FORMAT_UNDEFINED;
    }

    void ImGuiBackend::initRenderer(RenderBackend& renderBackend, rhi::TextureFormat colorFormat)
    {
        gBackend = renderBackend.getRHIBackend();

        if (gBackend == rhi::Backend::Vulkan)
        {
            const auto& vkDevice = static_cast<const vk::Device&>(renderBackend.getDevice());

            if (!vkDevice.getVkFeatures().dynamicRendering)
            {
                // 只对这台设备生效: 引擎的 Vulkan::CommandList 在没有 dynamic rendering
                // 时走的是经典 VkRenderPass + framebuffer 路径, ImGui 这边要接入就需要
                // 一个兼容的 VkRenderPass —— 对调试面板这个场景暂不实现, 直接优雅跳过。
                LOG_ERROR("[ImGuiBackend] Vulkan dynamic rendering not supported on this device; ImGui rendering disabled.");
                return;
            }

            vk::Device::QueueInfo queueInfo = vkDevice.getGraphicsQueueInfo();
            gVulkanColorFormatStorage = vk::convertToVkFormat(colorFormat);

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.ApiVersion = VK_API_VERSION_1_3;
            initInfo.Instance = vkDevice.getInstance();
            initInfo.PhysicalDevice = vkDevice.getPhysicalDevice();
            initInfo.Device = vkDevice.getHandle();
            initInfo.QueueFamily = queueInfo.familyIndex;
            initInfo.Queue = queueInfo.queue;
            initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE * 2;
            initInfo.MinImageCount = kMaxFramesInFlight;
            initInfo.ImageCount = kMaxFramesInFlight;
            initInfo.UseDynamicRendering = true;
            initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &gVulkanColorFormatStorage;

            gAvailable = ImGui_ImplVulkan_Init(&initInfo);
        }
        else
        {
            const auto& wgpuDevice = static_cast<const wgpu::Device&>(renderBackend.getDevice());

            ImGui_ImplWGPU_InitInfo initInfo{};
            initInfo.Device = wgpuDevice.getHandle();
            initInfo.NumFramesInFlight = kMaxFramesInFlight;
            initInfo.RenderTargetFormat = wgpu::convertTextureFormat(colorFormat);

            gAvailable = ImGui_ImplWGPU_Init(&initInfo);
        }
    }

    void ImGuiBackend::shutdown()
    {
        if (gAvailable)
        {
            if (gBackend == rhi::Backend::Vulkan)
                ImGui_ImplVulkan_Shutdown();
            else
                ImGui_ImplWGPU_Shutdown();
        }
        gAvailable = false;
    }

    void ImGuiBackend::renderDrawData(rhi::CommandList& cmd, ImDrawData* drawData)
    {
        if (!gAvailable || !drawData)
            return;

        // invokeNativeCallback() 按值接管这个临时 lambda, 把它 move 进自己的稳定存储
        // (CommandList::mNativeCallbacks), 调用方(这里)不需要再准备一份外部存储。
        cmd.invokeNativeCallback(
            [drawData](const rhi::NativePassHandle& handle)
            {
                if (handle.type == rhi::NativePassHandleType::Vulkan_Command_Buffer)
                {
                    ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(handle.handle));
                }
                else
                {
                    ImGui_ImplWGPU_RenderDrawData(drawData, static_cast<WGPURenderPassEncoder>(handle.handle));
                }
            }
        );
    }

    bool ImGuiBackend::isAvailable() noexcept
    {
        return gAvailable;
    }

    rhi::Backend ImGuiBackend::getBackend() noexcept
    {
        return gBackend;
    }

    void ImGuiBackend::newFrame()
    {
        if (!gAvailable)
            return;

        if (gBackend == rhi::Backend::Vulkan)
            ImGui_ImplVulkan_NewFrame();
        else
            ImGui_ImplWGPU_NewFrame();
    }
}
