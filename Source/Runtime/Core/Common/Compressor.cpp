#include "Compressor.h"

#include <algorithm>
#include <cassert>
#include <thread>

#include <zstd.h>

namespace hyp
{
    namespace
    {
        // 更高几档 level 会切到 zstd 更贵的 btopt/btultra2 策略, 边际收益很小
        constexpr int kZstdCompressionLevel = 12;

        // ZSTDMT 的任务粒度下限, 见 zstd.h 里 ZSTD_c_nbWorkers 的参数说明
        constexpr size_t kZstdMinJobSize = 512 * 1024;

        uint32_t computeCompressWorkerCount(size_t rawSize)
        {
            size_t maxUsefulJobs = rawSize / kZstdMinJobSize;
            if (maxUsefulJobs < 2)
                return 0;

            uint32_t hardwareThreads = std::thread::hardware_concurrency();
            if (hardwareThreads == 0)
                hardwareThreads = 4;

            return static_cast<uint32_t>(std::min<size_t>(maxUsefulJobs, hardwareThreads));
        }
    }

    size_t Compressor::calcCompressBound(size_t rawSize) noexcept
    {
        return 1 + ZSTD_compressBound(rawSize);
    }

    size_t Compressor::compress(std::span<const uint8_t> rawData, std::span<uint8_t> dst, Algorithm algorithm)
    {
        assert(algorithm == Algorithm::Zstd && "unsupported Compressor::Algorithm");
        assert(dst.size() >= calcCompressBound(rawData.size()));

        dst[0] = static_cast<uint8_t>(algorithm);

        ZSTD_CCtx* cctx = ZSTD_createCCtx();
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, kZstdCompressionLevel);
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, static_cast<int>(computeCompressWorkerCount(rawData.size())));
        size_t written = ZSTD_compress2(
            cctx,
            dst.data() + 1, dst.size() - 1,
            rawData.data(), rawData.size());
        ZSTD_freeCCtx(cctx);
        assert(!ZSTD_isError(written));
        return 1 + written;
    }

    size_t Compressor::getDecompressedSize(std::span<const uint8_t> compressedData) noexcept
    {
        assert(!compressedData.empty());
        unsigned long long size = ZSTD_getFrameContentSize(compressedData.data() + 1, compressedData.size() - 1);
        assert(size != ZSTD_CONTENTSIZE_UNKNOWN && size != ZSTD_CONTENTSIZE_ERROR);
        return static_cast<size_t>(size);
    }

    size_t Compressor::decompress(std::span<const uint8_t> compressedData, std::span<uint8_t> dst)
    {
        assert(!compressedData.empty());
        assert(static_cast<Algorithm>(compressedData[0]) == Algorithm::Zstd && "unsupported Compressor::Algorithm");
        assert(dst.size() >= getDecompressedSize(compressedData));

        size_t written = ZSTD_decompress(
            dst.data(), dst.size(),
            compressedData.data() + 1, compressedData.size() - 1);
        assert(!ZSTD_isError(written));
        return written;
    }

    std::vector<uint8_t> Compressor::compress(std::span<const uint8_t> rawData, Algorithm algorithm)
    {
        std::vector<uint8_t> result(calcCompressBound(rawData.size()));
        size_t written = compress(rawData, std::span<uint8_t>(result), algorithm);
        result.resize(written);
        return result;
    }

    std::vector<uint8_t> Compressor::decompress(std::span<const uint8_t> compressedData)
    {
        std::vector<uint8_t> result(getDecompressedSize(compressedData));
        size_t written = decompress(compressedData, std::span<uint8_t>(result));
        result.resize(written);
        return result;
    }
}
