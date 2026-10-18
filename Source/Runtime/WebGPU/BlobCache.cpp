#include "BlobCache.h"

#include <Core/Common/Hash.h>
#include <Platform/Platform.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace hyp::wgpu
{
    namespace
    {
        // Dawn 给的 key 是它内部算好的、不透明的变长字节串(翻译/编译输入的摘要), 引擎侧不需要
        // 理解其结构, 用 xxHash64 重新哈希一遍拿到定长文件名即可, 和 ShaderCache 的做法一致
        std::filesystem::path getCacheFilePath(const void* key, size_t keySize)
        {
            uint64_t hash = Hash::compute(key, keySize, Hash::Algorithm64::xxHash);
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
            std::filesystem::path dir = Platform::getExecutableDir() / "Cache" / "DawnCache" / std::string(hex, hex + 2);
            return dir / (std::string(hex) + ".bin");
        }

        // Dawn 的加载约定是两段式调用: 先传 value=nullptr 查询大小, 拿到大小后分配好缓冲区再传
        // value 真正取数据, 返回值是实际写入的字节数
        size_t onLoad(void const* key, size_t keySize, void* value, size_t valueSize, void* /*userdata*/)
        {
            std::filesystem::path path = getCacheFilePath(key, keySize);
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return 0;

            file.seekg(0, std::ios::end);
            std::streamoff fileSize = file.tellg();
            if (fileSize <= 0)
                return 0;
            file.seekg(0, std::ios::beg);

            if (value && valueSize > 0)
            {
                size_t readSize = std::min<size_t>(valueSize, static_cast<size_t>(fileSize));
                file.read(reinterpret_cast<char*>(value), readSize);
                return readSize;
            }
            return static_cast<size_t>(fileSize);
        }

        void onStore(void const* key, size_t keySize, void const* value, size_t valueSize, void* /*userdata*/)
        {
            std::filesystem::path path = getCacheFilePath(key, keySize);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
                return;

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
                return;
            file.write(reinterpret_cast<const char*>(value), valueSize);
        }
    }

    WGPUDawnCacheDeviceDescriptor createDawnCacheDeviceDescriptor()
    {
        WGPUDawnCacheDeviceDescriptor cacheDesc = WGPU_DAWN_CACHE_DEVICE_DESCRIPTOR_INIT;
        // isolationKey 相当于缓存的命名空间/版本号, 以后缓存约定变了(比如换了哈希算法、
        // 或者需要让所有旧缓存失效)改这个字符串就行, 不需要手动清磁盘
        cacheDesc.isolationKey = WGPUStringView("HyperEngine.v1", WGPU_STRLEN);
        cacheDesc.loadDataFunction = onLoad;
        cacheDesc.storeDataFunction = onStore;
        cacheDesc.functionUserdata = nullptr;
        return cacheDesc;
    }
}
