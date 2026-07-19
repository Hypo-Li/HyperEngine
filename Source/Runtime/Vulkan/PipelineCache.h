#pragma once
#include <volk.h>

namespace hyp::vk
{
    class Device;

    // 持久化到磁盘(DERIVED_DATA_DIR/PipelineCache)的 VkPipelineCache 包装, 纯粹是驱动内部
    // pipeline 编译结果的加速缓存, 不影响任何正确性——删掉磁盘上的文件不会导致功能问题,
    // 只是下次冷启动时 pipeline 又要重新走一遍驱动编译。不对外(RHI 接口)暴露, 仅在
    // Vulkan 后端内部创建 VkPipeline 时使用
    class PipelineCache
    {
    public:
        explicit PipelineCache(const Device& device);
        ~PipelineCache();

        VkPipelineCache getHandle() const noexcept
        {
            return mCache;
        }

    private:
        const Device& mDevice;
        VkPipelineCache mCache = VK_NULL_HANDLE;
    };
}
