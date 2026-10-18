#pragma once
#include "VertexLayout.h"

#include <Core/Common/DerivedDataCache.h>
#include <Core/Common/HString.h>
#include <RHI/ShaderModule.h>

#include <filesystem>
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

        ShaderCache(
            RenderTagRegistry& renderTagRegistry,
            const std::filesystem::path& shaderDir,
            const rhi::ShaderCompileTarget& shaderCompileTarget
        );

        // 依赖哈希按 (renderTag, 顶点布局类型) 缓存, 同一组合下的所有材质共用一份
        // withDebugInfo 会影响 Slang 实际吐出的字节码(带不带调试符号),必须算进 key 里——不然 Debug/
        // Release 两个配置共用同一个缓存目录时,先编译过的那个配置的产物会被另一个配置误命中
        // 缓存复用,Release 可能因此意外带着调试信息,或者 Debug 用到不带调试信息、没法在 RenderDoc/PIX
        // 里对应回源码的版本
        uint128_t computeVariantKey(
            HString renderTag,
            VertexLayout vertexLayout,
            const std::string& materialImplementation,
            std::span<const ShaderMacro> shaderMacros,
            bool withDebugInfo
        );

        std::optional<CachedVariant> tryLoad(uint128_t key) const;

        void save(uint128_t key, std::span<const uint8_t> vertexCode, std::span<const uint8_t> fragmentCode) const;

        // 编译 (name, source) 对应的 vertex+fragment 字节码, 成功后顺带调用 save() 落盘缓存。
        // IShaderCompiler 未注册时直接返回 nullopt, 不做任何事——调用方不需要自己先判断
        // hasCompiler() 再决定要不要走这条路径, 但如果已经确定要回退 ErrorMaterialShader
        // 就没必要再调用这个方法, hasCompiler() 可以用来提前短路
        std::optional<CachedVariant> compileAndCache(
            uint128_t key,
            const char* name,
            const std::string& source,
            const char* fileName,
            std::span<const ShaderMacro> shaderMacros,
            bool withDebugInfo
        ) const;

    private:
        // ContentKey:renderTag 模板文本哈希 + vertexLayout + 材质实现文本哈希 + 宏哈希 + target +
        // debugInfo, 纯字符串/字节哈希, 不需要 Slang, 编辑期和运行期都能算——是运行期只读查找烘焙
        // 数据时唯一用得到的 key
        uint128_t computeContentKey(
            HString renderTag,
            VertexLayout vertexLayout,
            const std::string& materialImplementation,
            std::span<const ShaderMacro> shaderMacros,
            bool withDebugInfo
        ) const;

        uint128_t getOrComputeDependencyHash(HString renderTag, VertexLayout vertexLayout);

    private:
        struct DependencyKey
        {
            HString renderTag;
            VertexLayoutType layoutType;

            bool operator==(const DependencyKey& other) const noexcept;
        };

        struct DependencyKeyHash
        {
            size_t operator()(const DependencyKey& key) const noexcept;
        };

        RenderTagRegistry& mRenderTagRegistry;
        std::filesystem::path mShaderDir;
        rhi::ShaderCompileTarget mShaderCompileTarget;
        std::unordered_map<DependencyKey, uint128_t, DependencyKeyHash> mDependencyHashes;
        DerivedDataCache mCache;
    };
}
