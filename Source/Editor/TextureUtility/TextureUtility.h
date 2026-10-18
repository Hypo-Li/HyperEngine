#pragma once
#include <Engine/Texture/TextureUtility.h>

namespace hyp
{
    bool isCompressionFormat(rhi::TextureFormat format) noexcept;

    uint32_t getBlockSizeByFormat(rhi::TextureFormat format) noexcept;

    uint32_t getPixelFormatChannels(PixelFormat format) noexcept;

    // ITextureUtility 唯一的具体实现——内部按压缩格式族分发到具体的第三方编码库(目前 BC 这一族
    // 里 BC1/BC3/BC4/BC5 用 rgbcx、BC6H/BC7 用 ispc_texcomp;以后加 ASTC/ETC2 等移动端格式族时
    // 在同一个类里继续加新的编码库依赖, 不因为换了个库就换一个实现类), mip 生成用自研可分离
    // 重采样(不依赖任何第三方库)。这个类只存在于编辑期工具链里, Engine 及其运行期消费方从不
    // #include 这个头文件、也从不链接这个 target。
    class TextureUtility : public ITextureUtility
    {
    public:
        std::vector<BakedMip> bakeMips(
            std::span<const uint8_t> mip0Data,
            const Texture& texture) override;
    };
}
