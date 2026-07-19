#pragma once
#include <filesystem>

#include "VertexLayout.h"
#include "RenderTagRegistry.h"
#include "MaterialTypes.h"
#include "ShaderCache.h"

#include <Core/Common/HString.h>
#include <RHI/Context.h>
#include <RHI/Device.h>
#include <RHI/Pipeline.h>

namespace hyp
{
    struct MaterialParameterMetadata
    {
        HString name;
        MaterialParameterType type;
    };

    struct ShaderVariant
    {
        TRef<rhi::ShaderModule> vertexModule;
        TRef<rhi::ShaderModule> fragmentModule;
        TRef<rhi::ShaderModule> computeModule;

        bool operator==(const ShaderVariant& other) const noexcept
        {
            return vertexModule == other.vertexModule
                && fragmentModule == other.fragmentModule
                && computeModule == other.computeModule;
        }
    };


    struct ShaderMacro
    {
        HString name;
        HString value;

        ShaderMacro() = default;

        ShaderMacro(HString n, HString v = "") : name(n), value(v) {}

        bool operator==(const ShaderMacro& other) const noexcept
        {
            return name == other.name && value == other.value;
        }
    };

    template <> Type* Reflection::createType<ShaderMacro>();

}

namespace std
{
    template <>
    struct hash<hyp::ShaderVariant>
    {
        size_t operator()(const hyp::ShaderVariant& variant) const noexcept
        {
            size_t seed = 0;
            hyp::Hash::combine(seed, variant.vertexModule.get());
            hyp::Hash::combine(seed, variant.fragmentModule.get());
            hyp::Hash::combine(seed, variant.computeModule.get());
            return seed;
        }
    };

    template <>
    struct hash<std::vector<hyp::ShaderMacro>>
    {
        size_t operator()(const std::vector<hyp::ShaderMacro>& macros) const noexcept
        {
            size_t seed = 0;
            for (auto& macro : macros)
            {
                size_t nameHash = macro.name.getTypeHash();
                size_t valueHash = macro.value.getTypeHash();
                hyp::Hash::combine(seed, nameHash);
                hyp::Hash::combine(seed, valueHash);
            }
            return seed;
        }
    };
}

namespace hyp
{
    class MaterialShader
    {
    public:
        MaterialShader(
            rhi::Backend rhiBackend,
            const rhi::Device& device,
            RenderTagRegistry& renderTagRegistry,
            ShaderCache& shaderCache,
            HString name,
            std::string&& materialImplementation
        );

        // shaderMacros 是每次调用才给的(不再是构造期固定的),同一个 MaterialShader 可以被多个
        // MaterialProxy(不同的 Template 默认值/Instance 覆写组合)共用,各自要各自的宏组合变体,
        // 所以宏也要进 VariantKey,否则不同宏组合会互相顶掉对方在 mVariants 里缓存的结果
        ShaderVariant getOrCreateVariant(HString renderTag, VertexLayout vertexLayout, std::span<const ShaderMacro> shaderMacros);

    private:
        ShaderVariant createVariant(HString renderTag, VertexLayout vertexLayout, std::span<const ShaderMacro> shaderMacros);

        std::string makeShaderSource(HString renderTag, VertexLayout vertexLayout);

    private:
        rhi::Backend mRHIBackend;
        const rhi::Device& mDevice;
        RenderTagRegistry& mRenderTagRegistry;
        ShaderCache& mShaderCache;
        HString mName;
        // 参数结构体定义(makeParameterDefines) + 顶点位置偏移函数(用户写的或默认返回 0 的) +
        // 材质着色函数(evaluateMaterial), 三段拼在一起、整体 splice 进 shader 模板的
        // [MATERIAL_IMPLEMENTATION] 标记处——之所以不拆成两个独立字符串分别维护, 是因为顶点位置
        // 偏移函数同样需要读取材质参数(比如风力强度这种标量), 必须和参数结构体定义在同一个作用域里,
        // 拆开反而要在两处都重复拼接参数定义
        std::string mMaterialImplementation;

        struct VariantKey
        {
            HString renderTag;
            VertexLayout vertexLayout;
            std::vector<ShaderMacro> shaderMacros;

            bool operator==(const VariantKey& other) const noexcept;
        };

        struct VariantKeyHash
        {
            std::size_t operator()(const VariantKey& key) const noexcept;
        };

        std::unordered_map<VariantKey, ShaderVariant, VariantKeyHash> mVariants;
    };

    class MaterialShaderHandle
    {
    public:
        MaterialShaderHandle() = default;

        MaterialShaderHandle(uint32_t index, uint32_t version)
            : mIndex(index), mVersion(version)
        {
        }

        bool operator==(const MaterialShaderHandle& other) const noexcept
        {
            return mIndex == other.mIndex && mVersion == other.mVersion;
        }

        uint32_t getIndex() const noexcept
        {
            return mIndex;
        }

        uint32_t getVersion() const noexcept
        {
            return mVersion;
        }

        bool isValid() const noexcept
        {
            return mIndex != ~0u && mVersion != ~0u;
        }

    private:
        uint32_t mIndex = ~0u;
        uint32_t mVersion = ~0u;
    };

    class MaterialShaderManager
    {
    public:
        MaterialShaderManager(rhi::Backend rhiBackend, const rhi::Device& device, RenderTagRegistry& renderTagRegistry);

        // Logic thread methods:

        MaterialShaderHandle allocateHandle();

        void freeHandle(MaterialShaderHandle handle);

        // Render thread methods:

        void createMaterialShader(
            MaterialShaderHandle handle,
            HString name,
            const std::string& materialSource,
            const std::string& vertexPositionOffsetSource,
            std::span<const MaterialParameterMetadata> parameterMetadata
        );

        void refMaterialShader(MaterialShaderHandle handle);

        void unrefMaterialShader(MaterialShaderHandle handle);

        MaterialShader* getMaterialShader(MaterialShaderHandle handle) const;

        // RenderTagFlags::Geometry_Only 的 render tag 下,静态不透明且无顶点位置偏移的材质会被
        // MaterialProxy 重定向到这里返回的共享 shader,而不是各自的 MaterialShader——每个 renderTag
        // 只懒创建一份,materialImplementation 是规范化的空实现(不含材质参数、不含 evaluateMaterial,
        // 只有默认零顶点位置偏移函数),因此所有 eligible 材质喂给 ShaderCache 的源码文本完全相同,
        // 天然在内容寻址缓存里收敛成同一份编译产物
        MaterialShader* getOrCreateSharedGeometryShader(HString renderTag);

    private:
        rhi::Backend mRHIBackend;
        const rhi::Device& mDevice;
        RenderTagRegistry& mRenderTagRegistry;
        ShaderCache mShaderCache;
        bool mBindlessEnabled = false;
        std::vector<uint32_t> mVersions;
        std::vector<uint32_t> mFreeIndices;
        struct Slot
        {
            std::unique_ptr<MaterialShader> shader;
            uint32_t refCount = 0;
            uint32_t version = 0;
        };
        std::vector<Slot> mSlots;
        std::unordered_map<HString, std::unique_ptr<MaterialShader>> mSharedGeometryShaders;
    };

    struct PassShaderBytecode
    {
        rhi::ShaderCodeFormat codeFormat;
        std::span<const uint8_t> vertex;
        std::span<const uint8_t> fragment;
        std::span<const uint8_t> compute;
    };

    // PassShader 对应的是引擎内置的固定 shader(Atmosphere/GSplat 等 pass 自己用的,
    // 不是材质),源码在构建期就已知、不会随用户内容变化,因此直接消费构建期由
    // slangc 预编译好的字节码,不再链接/调用 Slang,也不支持宏变体(现有 PassShader
    // 从未真正用到过宏)
    class PassShader
    {
    public:
        PassShader(
            const rhi::Device& device,
            HString name,
            const PassShaderBytecode& bytecode
        );

        const ShaderVariant& getVariant() const noexcept
        {
            return mVariant;
        }

    private:
        ShaderVariant mVariant;
    };
}
