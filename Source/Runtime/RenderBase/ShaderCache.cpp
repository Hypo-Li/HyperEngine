#include "ShaderCache.h"
#include "RenderTagRegistry.h"
#include "ShaderCompiler.h"

#include <Core/Common/Hash.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace hyp
{
    namespace
    {
        uint64_t hashStringContent(std::string_view str)
        {
            return Hash::compute(str, Hash::Algorithm::xxHash64);
        }

        uint64_t hashShaderMacros(std::span<const ShaderMacro> shaderMacros)
        {
            uint64_t seed = 0;
            for (const auto& macro : shaderMacros)
            {
                Hash::combine(seed, hashStringContent(macro.name.toStringView()));
                Hash::combine(seed, hashStringContent(macro.value.toStringView()));
            }
            return seed;
        }

        std::filesystem::path getCacheFilePath(uint64_t key)
        {
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(key));
            std::filesystem::path dir = std::filesystem::path(DERIVED_DATA_DIR) / "Shaders" / std::string(hex, hex + 2);
            return dir / (std::string(hex) + ".bin");
        }

        bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& outData)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;
            outData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }
    }

    ShaderCache::ShaderCache(RenderTagRegistry& renderTagRegistry) :
        mRenderTagRegistry(renderTagRegistry)
    {
    }

    uint64_t ShaderCache::computeVariantKey(
        HString renderTag,
        VertexLayout vertexLayout,
        const std::string& materialImplementation,
        std::span<const ShaderMacro> shaderMacros,
        rhi::ShaderCodeFormat codeFormat,
        bool withDebugInfo,
        const std::function<std::string()>& buildCombinedSource
    )
    {
        uint64_t renderTagHash = hashStringContent(mRenderTagRegistry.getShaderTemplate(renderTag));
        uint64_t materialImplementationHash = hashStringContent(materialImplementation);
        uint64_t macrosHash = hashShaderMacros(shaderMacros);

        // 依赖文件列表(#include/import 展开出来的那些)只取决于源码本身,和要不要带调试信息无关,
        // 所以 withDebugInfo 不进 memoKey,省得同一份源码因为 Debug/Release 两种配置各跑一遍
        // getDependencyFiles(重复调用 Slang 解析依赖)
        uint64_t memoKey = 0;
        Hash::combine(memoKey, renderTagHash);
        Hash::combine(memoKey, materialImplementationHash);
        Hash::combine(memoKey, macrosHash);
        Hash::combine(memoKey, codeFormat);

        uint64_t dependencyHash = getOrComputeDependencyHash(memoKey, renderTag, shaderMacros, codeFormat, buildCombinedSource);

        uint64_t key = 0;
        Hash::combine(key, renderTagHash);
        Hash::combine(key, vertexLayout.getType());
        Hash::combine(key, vertexLayout.getAttributeMask());
        Hash::combine(key, materialImplementationHash);
        Hash::combine(key, macrosHash);
        Hash::combine(key, codeFormat);
        Hash::combine(key, dependencyHash);
        Hash::combine(key, withDebugInfo);
        return key;
    }

    uint64_t ShaderCache::getOrComputeDependencyHash(
        uint64_t memoKey,
        HString renderTag,
        std::span<const ShaderMacro> shaderMacros,
        rhi::ShaderCodeFormat codeFormat,
        const std::function<std::string()>& buildCombinedSource
    )
    {
        auto it = mDependencyHashMemo.find(memoKey);
        if (it != mDependencyHashMemo.end())
        {
            return it->second;
        }

        std::string combinedSource = buildCombinedSource();
        const std::string& shaderFileName = mRenderTagRegistry.getShaderFileName(renderTag);
        bool forWgsl = codeFormat == rhi::ShaderCodeFormat::Wgsl;

        std::vector<std::string> dependencyFiles = ShaderCompiler::getDependencyFiles(
            "MaterialShader",
            combinedSource.c_str(),
            shaderFileName.c_str(),
            shaderMacros,
            forWgsl
        );
        std::sort(dependencyFiles.begin(), dependencyFiles.end());

        uint64_t dependencyHash = 0;
        for (const auto& depFile : dependencyFiles)
        {
            std::vector<uint8_t> content;
            if (readFile(depFile, content))
            {
                uint64_t fileHash = Hash::compute(content.data(), content.size(), Hash::Algorithm::xxHash64);
                Hash::combine(dependencyHash, fileHash);
            }
        }

        mDependencyHashMemo.emplace(memoKey, dependencyHash);
        return dependencyHash;
    }

    std::optional<ShaderCache::CachedVariant> ShaderCache::tryLoad(uint64_t key) const
    {
        std::filesystem::path path = getCacheFilePath(key);
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::nullopt;

        uint32_t vertexSize = 0;
        file.read(reinterpret_cast<char*>(&vertexSize), sizeof(vertexSize));
        if (!file)
            return std::nullopt;

        CachedVariant result;
        result.vertexCode.resize(vertexSize);
        file.read(reinterpret_cast<char*>(result.vertexCode.data()), vertexSize);
        if (!file)
            return std::nullopt;

        uint32_t fragmentSize = 0;
        file.read(reinterpret_cast<char*>(&fragmentSize), sizeof(fragmentSize));
        if (!file)
            return std::nullopt;

        result.fragmentCode.resize(fragmentSize);
        file.read(reinterpret_cast<char*>(result.fragmentCode.data()), fragmentSize);
        if (!file)
            return std::nullopt;

        return result;
    }

    void ShaderCache::save(uint64_t key, std::span<const uint8_t> vertexCode, std::span<const uint8_t> fragmentCode) const
    {
        std::filesystem::path path = getCacheFilePath(key);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return;

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return;

        uint32_t vertexSize = static_cast<uint32_t>(vertexCode.size());
        file.write(reinterpret_cast<const char*>(&vertexSize), sizeof(vertexSize));
        file.write(reinterpret_cast<const char*>(vertexCode.data()), vertexCode.size());

        uint32_t fragmentSize = static_cast<uint32_t>(fragmentCode.size());
        file.write(reinterpret_cast<const char*>(&fragmentSize), sizeof(fragmentSize));
        file.write(reinterpret_cast<const char*>(fragmentCode.data()), fragmentCode.size());
    }
}
