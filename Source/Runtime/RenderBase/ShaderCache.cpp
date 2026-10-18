#include "ShaderCache.h"
#include "RenderTagRegistry.h"
#include "ShaderCompiler.h"

#include <Core/Common/Hash.h>
#include <Core/Common/LogSystem.h>
#include <Core/Common/ModuleServiceRegistry.h>
#include <Platform/Platform.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace hyp
{
    namespace
    {
        uint128_t hashStringContent(std::string_view str)
        {
            return Hash::compute(str, Hash::Algorithm128::xxHash);
        }

        uint128_t hashShaderMacros(std::span<const ShaderMacro> shaderMacros)
        {
            uint128_t seed{};
            for (const auto& macro : shaderMacros)
            {
                Hash::combine(seed, hashStringContent(macro.name.toStringView()));
                Hash::combine(seed, hashStringContent(macro.value.toStringView()));
            }
            return seed;
        }

        bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& outData)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;

            file.seekg(0, std::ios::end);
            size_t size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);

            outData.resize(size);
            file.read(reinterpret_cast<char*>(outData.data()), size);
            return true;
        }

        // 仅用于让 renderTag 模板通过 Slang 解析以枚举 #include 依赖
        constexpr const char* kDependencyProbeMaterialImplementation =
            "float3 evaluateVertexPositionOffset(in VertexPositionOffsetInput vpoi) { return float3(0, 0, 0); }\n"
            "void evaluateMaterial(in MaterialInput mi, inout MaterialOutput mo) {}\n";

        std::array<VertexLayout, 2> makeDependencyProbeLayouts()
        {
            const VertexAttributeMask commonMask =
                VertexAttributeMask::Position | VertexAttributeMask::Normal | VertexAttributeMask::Tangent | VertexAttributeMask::TexCoord0;
            return {
                VertexLayout(VertexLayoutType::Static_Mesh, commonMask),
                VertexLayout(VertexLayoutType::Skeletal_Mesh, commonMask | VertexAttributeMask::Bone_Weight0)
            };
        }
    }

    ShaderCache::ShaderCache(
        RenderTagRegistry& renderTagRegistry,
        const std::filesystem::path& shaderDir,
        const rhi::ShaderCompileTarget& shaderCompileTarget
    ) :
        mRenderTagRegistry(renderTagRegistry),
        mShaderDir(shaderDir),
        mShaderCompileTarget(shaderCompileTarget),
        mCache(Platform::getExecutableDir() / "Cache", "Shaders")
    {
        if (ModuleServiceRegistry::get().getService<IShaderCompiler>())
        {
            for (HString renderTag : mRenderTagRegistry.getRenderTags())
            {
                for (VertexLayout probeLayout : makeDependencyProbeLayouts())
                {
                    getOrComputeDependencyHash(renderTag, probeLayout);
                }
            }
        }
    }

    uint128_t ShaderCache::computeVariantKey(
        HString renderTag,
        VertexLayout vertexLayout,
        const std::string& materialImplementation,
        std::span<const ShaderMacro> shaderMacros,
        bool withDebugInfo
    )
    {
        uint128_t key = computeContentKey(renderTag, vertexLayout, materialImplementation, shaderMacros, withDebugInfo);

        // dependencyHash 只在有具体 IShaderCompiler 实现时才有意义(用来查询 #include 依赖、
        // 让公共文件改动使本地 DDC 自然失效)——运行期(没有注册 IShaderCompiler 的配置)没有
        // 能力问依赖, 发布产物也是打包时刻的固定快照, 不存在这种失效场景, 所以运行期查找只用
        // 上面这份 ContentKey, 完全跳过这一步, 不是"当 dependencyHash 为 0 处理"
        if (ModuleServiceRegistry::get().getService<IShaderCompiler>())
        {
            Hash::combine(key, getOrComputeDependencyHash(renderTag, vertexLayout));
        }

        return key;
    }

    uint128_t ShaderCache::computeContentKey(
        HString renderTag,
        VertexLayout vertexLayout,
        const std::string& materialImplementation,
        std::span<const ShaderMacro> shaderMacros,
        bool withDebugInfo
    ) const
    {
        uint128_t renderTagHash = hashStringContent(mRenderTagRegistry.getShaderTemplate(renderTag));
        uint128_t materialImplementationHash = hashStringContent(materialImplementation);
        uint128_t macrosHash = hashShaderMacros(shaderMacros);

        uint128_t key{};
        Hash::combine(key, renderTagHash);
        Hash::combine(key, vertexLayout.getType());
        Hash::combine(key, vertexLayout.getAttributeMask());
        Hash::combine(key, hashStringContent(vertexLayout.getVertexAttributeImplementation()));
        Hash::combine(key, materialImplementationHash);
        Hash::combine(key, macrosHash);
        Hash::combine(key, mShaderCompileTarget.codeFormat);
        Hash::combine(key, mShaderCompileTarget.majorVersion);
        Hash::combine(key, mShaderCompileTarget.minorVersion);
        Hash::combine(key, withDebugInfo);
        return key;
    }

    uint128_t ShaderCache::getOrComputeDependencyHash(HString renderTag, VertexLayout vertexLayout)
    {
        const DependencyKey dependencyKey = { renderTag, vertexLayout.getType() };
        auto it = mDependencyHashes.find(dependencyKey);
        if (it != mDependencyHashes.end())
        {
            return it->second;
        }

        std::string probeShaderSource = mRenderTagRegistry.makeShaderSource(renderTag, kDependencyProbeMaterialImplementation, vertexLayout);
        const std::string& shaderFileName = mRenderTagRegistry.getShaderFileName(renderTag);

        std::vector<std::string> dependencyFiles = ModuleServiceRegistry::get().getService<IShaderCompiler>()->getDependencyFiles(
            "MaterialShader",
            probeShaderSource.c_str(),
            shaderFileName.c_str(),
            mShaderDir,
            {},
            mShaderCompileTarget
        );
        if (dependencyFiles.empty())
        {
            LOG_ERROR("ShaderCache: renderTag {} (顶点布局类型 {}) 的依赖文件解析结果为空, 模板可能没有通过 Slang 解析", renderTag.toStringView(), static_cast<int>(vertexLayout.getType()));
        }
        std::sort(dependencyFiles.begin(), dependencyFiles.end());

        uint128_t dependencyHash{};
        for (const auto& depFile : dependencyFiles)
        {
            std::vector<uint8_t> content;
            if (readFile(depFile, content))
            {
                uint128_t fileHash = Hash::compute(content.data(), content.size(), Hash::Algorithm128::xxHash);
                Hash::combine(dependencyHash, fileHash);
            }
        }

        mDependencyHashes.emplace(dependencyKey, dependencyHash);
        return dependencyHash;
    }

    bool ShaderCache::DependencyKey::operator==(const DependencyKey& other) const noexcept
    {
        return renderTag == other.renderTag && layoutType == other.layoutType;
    }

    size_t ShaderCache::DependencyKeyHash::operator()(const DependencyKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.renderTag);
        Hash::combine(seed, key.layoutType);
        return seed;
    }

    std::optional<ShaderCache::CachedVariant> ShaderCache::tryLoad(uint128_t key) const
    {
        std::optional<std::vector<uint8_t>> blob = mCache.tryLoad(key);
        if (!blob)
            return std::nullopt;

        // 帧格式: [u32 vertexSize][vertexBytes][u32 fragmentSize][fragmentBytes] ——
        // DerivedDataCache 只管原始字节的存取, 这个两段变长 blob 的组帧/拆帧是 ShaderCache 自己的事
        size_t offset = 0;
        auto readChunk = [&](std::vector<uint8_t>& out) -> bool
        {
            if (offset + sizeof(uint32_t) > blob->size())
                return false;
            uint32_t size;
            std::memcpy(&size, blob->data() + offset, sizeof(size));
            offset += sizeof(size);
            if (offset + size > blob->size())
                return false;
            out.assign(blob->begin() + offset, blob->begin() + offset + size);
            offset += size;
            return true;
        };

        CachedVariant result;
        if (!readChunk(result.vertexCode) || !readChunk(result.fragmentCode))
            return std::nullopt;

        return result;
    }

    void ShaderCache::save(uint128_t key, std::span<const uint8_t> vertexCode, std::span<const uint8_t> fragmentCode) const
    {
        std::vector<uint8_t> blob;
        blob.reserve(sizeof(uint32_t) * 2 + vertexCode.size() + fragmentCode.size());
        auto writeChunk = [&](std::span<const uint8_t> data)
        {
            uint32_t size = static_cast<uint32_t>(data.size());
            const uint8_t* sizeBytes = reinterpret_cast<const uint8_t*>(&size);
            blob.insert(blob.end(), sizeBytes, sizeBytes + sizeof(size));
            blob.insert(blob.end(), data.begin(), data.end());
        };
        writeChunk(vertexCode);
        writeChunk(fragmentCode);

        mCache.save(key, blob);
    }

    std::optional<ShaderCache::CachedVariant> ShaderCache::compileAndCache(
        uint128_t key,
        const char* name,
        const std::string& source,
        const char* fileName,
        std::span<const ShaderMacro> shaderMacros,
        bool withDebugInfo
    ) const
    {
        IShaderCompiler* compiler = ModuleServiceRegistry::get().getService<IShaderCompiler>();
        if (!compiler)
            return std::nullopt;

        IShaderCompiler::ShaderStage stages[] = {
            IShaderCompiler::ShaderStage::Vertex,
            IShaderCompiler::ShaderStage::Fragment
        };

        // 光带调试符号不关优化, 调试器里局部变量会大量显示不可用
        const bool optimize = !withDebugInfo;

        std::vector<std::vector<uint8_t>> shaderCode;
        if (mShaderCompileTarget.codeFormat == rhi::ShaderCodeFormat::Spirv)
        {
            shaderCode = compiler->compileToSpirvs(name, source.c_str(), stages, fileName, mShaderDir, mShaderCompileTarget, shaderMacros, optimize, withDebugInfo);
        }
        else if (mShaderCompileTarget.codeFormat == rhi::ShaderCodeFormat::Wgsl)
        {
            shaderCode = compiler->compileToWgsls(name, source.c_str(), stages, fileName, mShaderDir, mShaderCompileTarget, shaderMacros, optimize, withDebugInfo);
        }

        if (shaderCode.size() < 2)
            return std::nullopt;

        save(key, shaderCode[0], shaderCode[1]);

        CachedVariant result;
        result.vertexCode = std::move(shaderCode[0]);
        result.fragmentCode = std::move(shaderCode[1]);
        return result;
    }
}
