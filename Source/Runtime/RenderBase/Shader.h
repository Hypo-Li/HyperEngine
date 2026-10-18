#pragma once
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_set>

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
            const std::filesystem::path& shaderDir,
            HString name,
            std::string&& materialImplementation
        );

        // shaderMacros 是每次调用才给的(不再是构造期固定的),同一个 MaterialShader 可以被多个
        // MaterialProxy(不同的 Template 默认值/Instance 覆写组合)共用,各自要各自的宏组合变体,
        // 所以宏也要进 VariantKey,否则不同宏组合会互相顶掉对方在 mVariants 里缓存的结果
        //
        // 返回 nullopt 表示这个变体既没有命中本地/烘焙缓存、也没有可用的 IShaderCompiler 编译它——
        // 调用方(MaterialProxy::getOrCreateShaderVariant)需要据此回退到
        // MaterialShaderManager::getErrorMaterialShader()
        std::optional<ShaderVariant> getOrCreateVariant(HString renderTag, VertexLayout vertexLayout, std::span<const ShaderMacro> shaderMacros);

    private:
        std::optional<ShaderVariant> createVariant(HString renderTag, VertexLayout vertexLayout, std::span<const ShaderMacro> shaderMacros);

    private:
        rhi::Backend mRHIBackend;
        const rhi::Device& mDevice;
        RenderTagRegistry& mRenderTagRegistry;
        ShaderCache& mShaderCache;
        std::filesystem::path mShaderDir;
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

        // 失败的 key 缓存, 避免重复触发编译/打印同一条错误——换新 MaterialShader 实例时自然清空
        std::unordered_set<VariantKey, VariantKeyHash> mFailedVariants;
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
        MaterialShaderManager(rhi::Backend rhiBackend, const rhi::Device& device, RenderTagRegistry& renderTagRegistry, const std::filesystem::path& shaderDir);

        // Logic thread methods:

        // 索引什么时候能被复用完全由渲染线程说了算(见 unrefMaterialShader() 的说明)——
        // 这里不再有对称的 freeHandle(), 逻辑线程只管分配, 不管回收。
        MaterialShaderHandle allocateHandle();

        // Render thread methods:

        void createMaterialShader(
            MaterialShaderHandle handle,
            HString name,
            const std::string& materialFunctionLibrarySource,
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

        // 材质变体既没有本地/烘焙缓存、也没有可用编译器时的兜底——固定品红自发光、零材质参数的
        // MaterialShader, 走和正常材质完全一样的 renderTag 模板 + splice + 编译/缓存路径, 对任意
        // renderTag 都能产出签名匹配的 shader, 新增 render tag 不需要额外写兜底 shader。cook 阶段
        // 必须保证这个 shader 对所有 renderTag 都能编译成功并进 ShaderCache, 否则这里会 assert
        ShaderVariant getErrorMaterialShader(HString renderTag, VertexLayout vertexLayout);

    private:
        rhi::Backend mRHIBackend;
        const rhi::Device& mDevice;
        RenderTagRegistry& mRenderTagRegistry;
        ShaderCache mShaderCache;
        std::filesystem::path mShaderDir;
        bool mBindlessEnabled = false;
        std::unique_ptr<MaterialShader> mErrorMaterialShader;

        // mVersions/mReclaimedIndices 现在是逻辑线程(allocateHandle())和渲染线程
        // (unrefMaterialShader() 引用计数归零时)共同touch的状态, 用 mHandleMutex 保护——
        // 之前 mVersions/mFreeIndices 只由逻辑线程touch, 靠 freeHandle() 立刻把 index 塞回
        // 空闲列表, 但那时候渲染线程侧的 slot.refCount 不一定真的归零了(比如 MaterialInstance
        // 会额外 refMaterialShader() 一次), 复用同一个 index 建新 shader 时会撞上
        // assert(slot.shader == nullptr)。现在 index 只有在渲染线程确认"引用计数真的归零"
        // 之后才会被交还, 逻辑线程自己不再直接决定一个 index 能不能被回收。
        std::mutex mHandleMutex;
        std::vector<uint32_t> mVersions;
        std::vector<uint32_t> mReclaimedIndices;

        // mSlots 只由渲染线程 touch(createMaterialShader/refMaterialShader/
        // unrefMaterialShader/getMaterialShader 都只在渲染线程执行的 lambda 里被调用),
        // 不需要加锁。
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
