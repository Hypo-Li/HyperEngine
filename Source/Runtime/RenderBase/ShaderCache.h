#pragma once
#include "VertexLayout.h"

#include <Core/Common/HString.h>
#include <RHI/ShaderModule.h>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace hyp
{
    class RenderTagRegistry;
    struct ShaderMacro;

    // 材质 shader 字节码的本地内容寻址缓存,纯粹是编译结果的派生数据(不进材质资产),
    // 命中与否只影响是否要重新调用 Slang,算错了最多多编译一次,不会导致正确性问题
    class ShaderCache
    {
    public:
        struct CachedVariant
        {
            std::vector<uint8_t> vertexCode;
            std::vector<uint8_t> fragmentCode;
        };

        explicit ShaderCache(RenderTagRegistry& renderTagRegistry);

        // buildCombinedSource 只在本次运行第一次遇到 (renderTag, materialSource, macros, codeFormat)
        // 这个组合时才会被调用一次(用于向 Slang 查询 #include 依赖),后续同组合的调用直接复用缓存的依赖哈希,
        // 不会重新拼接 shader 源码
        // withDebugInfo 会影响 Slang 实际吐出的字节码(带不带调试符号),必须算进 key 里——不然 Debug/
        // Release 两个配置共用同一个 DERIVED_DATA_DIR 时,先编译过的那个配置的产物会被另一个配置误命中
        // 缓存复用,Release 可能因此意外带着调试信息,或者 Debug 用到不带调试信息、没法在 RenderDoc/PIX
        // 里对应回源码的版本
        uint64_t computeVariantKey(
            HString renderTag,
            VertexLayout vertexLayout,
            const std::string& materialImplementation,
            std::span<const ShaderMacro> shaderMacros,
            rhi::ShaderCodeFormat codeFormat,
            bool withDebugInfo,
            const std::function<std::string()>& buildCombinedSource
        );

        std::optional<CachedVariant> tryLoad(uint64_t key) const;

        void save(uint64_t key, std::span<const uint8_t> vertexCode, std::span<const uint8_t> fragmentCode) const;

    private:
        uint64_t getOrComputeDependencyHash(
            uint64_t memoKey,
            HString renderTag,
            std::span<const ShaderMacro> shaderMacros,
            rhi::ShaderCodeFormat codeFormat,
            const std::function<std::string()>& buildCombinedSource
        );

    private:
        RenderTagRegistry& mRenderTagRegistry;
        std::unordered_map<uint64_t, uint64_t> mDependencyHashMemo;
    };
}
