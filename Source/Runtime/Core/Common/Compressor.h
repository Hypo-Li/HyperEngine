#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace hyp
{
    class Compressor
    {
    public:
        enum class Algorithm : uint8_t
        {
            Zstd,
        };

        // 压缩结果最坏情况下的上界(含开头 1 字节算法标记), 不是精确大小——调用方按这个分配目标 buffer
        static size_t calcCompressBound(size_t rawSize) noexcept;

        // dst.size() 必须 >= calcCompressBound(rawData.size()); 返回实际写入的字节数
        static size_t compress(std::span<const uint8_t> rawData, std::span<uint8_t> dst, Algorithm algorithm = Algorithm::Zstd);

        // 从 blob 开头的算法标记 + 对应算法自身帧头中读出的精确原始大小, 不需要调用方额外提供
        static size_t getDecompressedSize(std::span<const uint8_t> compressedData) noexcept;

        // dst.size() 必须 >= getDecompressedSize(compressedData); 返回实际写入的字节数
        static size_t decompress(std::span<const uint8_t> compressedData, std::span<uint8_t> dst);

        // 便捷重载——内部按 calcCompressBound()/getDecompressedSize() 分配好 vector, 转调上面的核心版本
        static std::vector<uint8_t> compress(std::span<const uint8_t> rawData, Algorithm algorithm = Algorithm::Zstd);
        static std::vector<uint8_t> decompress(std::span<const uint8_t> compressedData);
    };
}
