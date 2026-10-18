#include "PipelineCache.h"
#include "Device.h"

#include <Core/Common/LogSystem.h>
#include <Platform/Platform.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace hyp::vk
{
    namespace
    {
        // 单个 VkPipelineCache blob 是驱动内部不透明状态, 引擎侧拿不到条目粒度的时间戳或
        // 引用计数, 没法像 ShaderCache 那样靠内容寻址 + 孤儿文件自然回收。超过这个大小就当作
        // 缓存已经失控增长了, 直接整文件丢弃重建, 而不是尝试精细清理
        constexpr uintmax_t kMaxCacheFileSize = 64ull * 1024 * 1024;

        // pipelineCacheUUID 编码了 GPU 型号 + 驱动版本, 用它做文件名可以让不同设备/驱动
        // 各自积累各自的缓存文件, 互不覆盖冲突; 驱动加载时也会用同一个 UUID 校验 blob 是否
        // 匹配当前设备, 不匹配会静默当空缓存处理, 不会报错
        std::filesystem::path getCacheFilePath(const uint8_t (&uuid)[VK_UUID_SIZE])
        {
            char hex[VK_UUID_SIZE * 2 + 1];
            for (uint32_t i = 0; i < VK_UUID_SIZE; ++i)
            {
                std::snprintf(hex + i * 2, 3, "%02x", uuid[i]);
            }
            return Platform::getExecutableDir() / "Cache" / "PipelineCache" / (std::string(hex, hex + VK_UUID_SIZE * 2) + ".bin");
        }
    }

    PipelineCache::PipelineCache(const Device& device) :
        mDevice(device)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device.getPhysicalDevice(), &properties);
        std::filesystem::path path = getCacheFilePath(properties.pipelineCacheUUID);

        std::vector<uint8_t> initialData;
        std::error_code sizeEc;
        uintmax_t fileSize = std::filesystem::file_size(path, sizeEc);
        if (!sizeEc && fileSize > 0 && fileSize <= kMaxCacheFileSize)
        {
            std::ifstream file(path, std::ios::binary);
            if (file)
            {
                initialData.resize(static_cast<size_t>(fileSize));
                file.read(reinterpret_cast<char*>(initialData.data()), static_cast<std::streamsize>(fileSize));
            }
        }

        VkPipelineCacheCreateInfo cacheInfo = {};
        cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        cacheInfo.initialDataSize = initialData.size();
        cacheInfo.pInitialData = initialData.empty() ? nullptr : initialData.data();

        if (vkCreatePipelineCache(device.getHandle(), &cacheInfo, nullptr, &mCache) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create Vulkan pipeline cache");
        }
    }

    PipelineCache::~PipelineCache()
    {
        if (mCache == VK_NULL_HANDLE)
            return;

        size_t dataSize = 0;
        vkGetPipelineCacheData(mDevice.getHandle(), mCache, &dataSize, nullptr);

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(mDevice.getPhysicalDevice(), &properties);
        std::filesystem::path path = getCacheFilePath(properties.pipelineCacheUUID);

        if (dataSize > kMaxCacheFileSize)
        {
            // 已经超出阈值: 不再写盘, 直接删掉旧文件, 下次启动就是空缓存重新开始积累,
            // 而不是让文件无限增长下去
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        else if (dataSize > 0)
        {
            std::vector<uint8_t> data(dataSize);
            if (vkGetPipelineCacheData(mDevice.getHandle(), mCache, &dataSize, data.data()) == VK_SUCCESS)
            {
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                if (!ec)
                {
                    std::ofstream file(path, std::ios::binary | std::ios::trunc);
                    if (file)
                    {
                        file.write(reinterpret_cast<const char*>(data.data()), data.size());
                    }
                }
            }
        }

        vkDestroyPipelineCache(mDevice.getHandle(), mCache, nullptr);
    }
}
