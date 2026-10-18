#pragma once
#include "Texture.h"

#include <cstdint>
#include <vector>

namespace hyp
{
    // 纹理压缩 + mip 生成能力的抽象接口——具体实现住在 Source/Editor/TextureUtility,
    // 编辑期专属工具库, Engine 作为更底层的库不能反过来依赖它, 只能通过 ModuleServiceRegistry
    // (见 Core/Common/ModuleServiceRegistry.h)按接口类型查找已注册的实现。运行期(未构建/未链接
    // Editor 工具链的配置)不存在任何实现, ModuleServiceRegistry::getService<ITextureUtility>()
    // 返回 nullptr。
    class ITextureUtility
    {
    public:
        struct BakedMip
        {
            uint32_t level;
            std::vector<uint8_t> data;
        };

        virtual ~ITextureUtility() = default;

        // mip0Data 是 texture 的 Description 描述的原始像素(单层, 未压缩)。返回值按 level 升序
        // 排列, 每一级都已经是最终格式(该压缩的已经压缩, 不该压缩的保持原始像素排布)——
        // 调用方(Texture::bakeMips())只管把结果原样塞进 mMips, 不做任何二次处理
        virtual std::vector<BakedMip> bakeMips(
            std::span<const uint8_t> mip0Data,
            const Texture& texture) = 0;
    };
}
