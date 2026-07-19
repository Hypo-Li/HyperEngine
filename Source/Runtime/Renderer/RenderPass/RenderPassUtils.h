#pragma once
#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>
#include <cassert>
#include <cstring>

#include "../RendererContext.h"

#include <RHI/BindGroup.h>
#include <RHI/Pipeline.h>
#include <RHI/CommandList.h>

namespace hyp
{
    template<size_t HandleCount>
    class BindGroupCache
    {
    public:
        template <typename... Handles>
        bool needUpdate(Handles... currentHandles)
        {
            static_assert(sizeof...(Handles) == HandleCount,
                "Number of handles must match template parameter");
            std::array<uint64_t, HandleCount> currentHandleValues{ handleToUint64(currentHandles)... };
            bool needUpdate = !mBindGroup || mLastHandles != currentHandleValues;
            if (needUpdate)
            {
                mPendingHandles = currentHandleValues;
            }
            return needUpdate;
        }

        template <typename WriteFn>
        void update(
            RenderBackend& renderBackend,
            const rhi::BindGroupLayout& layout,
            WriteFn&& writeCallback
        )
        {
            auto newBindGroup = renderBackend.getDevice().createBindGroup(layout);
            std::forward<WriteFn>(writeCallback)(*newBindGroup);

            if (mBindGroup)
            {
                renderBackend.getDeferredTaskQueue().push(
                    [oldGroup = std::move(mBindGroup)]() mutable { oldGroup = nullptr; },
                    kMaxFramesInFlight
                );
            }

            mBindGroup = newBindGroup;
            mLastHandles = mPendingHandles;
        }

        rhi::BindGroup* getBindGroup() const noexcept { return mBindGroup.get(); }

    private:
        static uint64_t handleToUint64(const rhi::Buffer* buffer) noexcept { return reinterpret_cast<uint64_t>(buffer); }
        static uint64_t handleToUint64(const rhi::TextureView* textureView) noexcept { return reinterpret_cast<uint64_t>(textureView); }
        static uint64_t handleToUint64(const rhi::Sampler* sampler) noexcept { return reinterpret_cast<uint64_t>(sampler); }
        static uint64_t handleToUint64(std::nullptr_t) noexcept { return 0; }

        TRef<rhi::BindGroup> mBindGroup;
        std::array<uint64_t, HandleCount> mLastHandles{};
        std::array<uint64_t, HandleCount> mPendingHandles{};
    };

    // 从 CompilePassShader.cmake 反射生成的 PassShaderBindingDesc[](见对应 .generated.h 里的
    // k<Name>_Bindings)直接建 BindGroupLayout, 取代过去每个 Pass 手写 BindGroupLayoutEntry[]
    // 的做法——binding/type/access/textureViewType/textureSampleType/stageFlags 这些信息现在
    // 只在 .slang 源码里的 [[vk::binding(N, 2)]] 声明一份, 不需要再在 C++ 里抄一份保持同步。
    inline TRef<rhi::BindGroupLayout> createPassBindGroupLayout(
        RenderBackend& renderBackend,
        std::span<const rhi::PassShaderBindingDesc> bindings)
    {
        std::vector<rhi::BindGroupLayoutEntry> entries;
        entries.reserve(bindings.size());
        for (const rhi::PassShaderBindingDesc& desc : bindings)
        {
            entries.push_back(desc.toBindGroupLayoutEntry());
        }
        return renderBackend.getBindGroupLayoutCache().getOrCreate(entries);
    }

    // 配合 createPassBindGroupLayout: 按 .slang 里的资源名字(而不是数字下标)写 BindGroupEntry,
    // 名字→binding 索引的查找表就是同一份 PassShaderBindingDesc[]。setXxx 只是把 (name, resource)
    // 缓存起来, 真正的 rhi::BindGroup::update() 调用要等 apply() 才发生——BindGroupEntry 的
    // buffers/textures/samplers 字段是 span, 指向的资源指针必须活到 update() 调用那一刻,
    // 构造时按 bindings.size() 预留好存储空间的 vector 容量, 保证中途 push_back 不会因为扩容
    // 让已经交出去的指针失效。
    class PassBindGroupWriter
    {
    public:
        explicit PassBindGroupWriter(std::span<const rhi::PassShaderBindingDesc> bindings) noexcept
            : mBindings(bindings)
        {
            mTextureStorage.reserve(bindings.size());
            mSamplerStorage.reserve(bindings.size());
            mBufferStorage.reserve(bindings.size());
        }

        PassBindGroupWriter& setTexture(std::string_view name, const rhi::TextureView* view)
        {
            const rhi::PassShaderBindingDesc& desc = findByName(name);
            mTextureStorage.push_back(view);

            rhi::BindGroupEntry entry;
            entry.binding = desc.binding;
            entry.type = desc.type;
            entry.index = 0;
            entry.textures = { &mTextureStorage.back(), 1 };
            mEntries.push_back(entry);
            return *this;
        }

        PassBindGroupWriter& setSampler(std::string_view name, const rhi::Sampler* sampler)
        {
            const rhi::PassShaderBindingDesc& desc = findByName(name);
            mSamplerStorage.push_back(sampler);

            rhi::BindGroupEntry entry;
            entry.binding = desc.binding;
            entry.type = desc.type;
            entry.index = 0;
            entry.samplers = { &mSamplerStorage.back(), 1 };
            mEntries.push_back(entry);
            return *this;
        }

        PassBindGroupWriter& setBuffer(std::string_view name, const rhi::Buffer* buffer, uint64_t offset = 0, uint64_t range = rhi::kWholeBuffer)
        {
            const rhi::PassShaderBindingDesc& desc = findByName(name);
            mBufferStorage.push_back(rhi::BufferBindingInfo{ buffer, offset, range });

            rhi::BindGroupEntry entry;
            entry.binding = desc.binding;
            entry.type = desc.type;
            entry.index = 0;
            entry.buffers = { &mBufferStorage.back(), 1 };
            mEntries.push_back(entry);
            return *this;
        }

        void apply(rhi::BindGroup& bindGroup) const
        {
            bindGroup.update(mEntries);
        }

    private:
        const rhi::PassShaderBindingDesc& findByName(std::string_view name) const
        {
            for (const rhi::PassShaderBindingDesc& desc : mBindings)
            {
                if (name == desc.name)
                {
                    return desc;
                }
            }
            assert(false && "PassBindGroupWriter: binding name not found in reflected PassShaderBindingDesc[] - check the .slang resource name matches the setXxx() call");
            return mBindings.front();
        }

        std::span<const rhi::PassShaderBindingDesc> mBindings;
        std::vector<const rhi::TextureView*> mTextureStorage;
        std::vector<const rhi::Sampler*> mSamplerStorage;
        std::vector<rhi::BufferBindingInfo> mBufferStorage;
        std::vector<rhi::BindGroupEntry> mEntries;
    };

    // 拼出 CompilePassShader.cmake 按 k<Name>_<Stage>_Spirv / k<Name>_<Stage>_Wgsl 规则生成的这对符号名,
    // 展开成 makeComputeBytecode/makeVertexFragmentBytecode 要的 span+string_view 参数对,调用点不用手写全名
    #define GET_PASS_SHADER_BYTECODE(name, stage) \
        std::span(generated::k##name##_##stage##_Spirv), std::string_view(generated::k##name##_##stage##_Wgsl)

    // 按当前后端从 spirv/wgsl 中选出实际要用的那一份,包成 PassShaderStageBytecode 的原始字节 span,
    // 不做拷贝(底层是构建期生成头文件里的 static 常量数组,生命周期覆盖整个程序)
    inline std::span<const uint8_t> selectStageBytecode(rhi::Backend backend, std::span<const uint32_t> spirv, std::string_view wgsl)
    {
        if (backend == rhi::Backend::WebGPU)
        {
            // 保留末尾的 '\0',和 ShaderCompiler::compileToWgsls 运行时编译出来的格式一致
            const uint8_t* begin = reinterpret_cast<const uint8_t*>(wgsl.data());
            return std::span<const uint8_t>(begin, wgsl.size() + 1);
        }
        const uint8_t* begin = reinterpret_cast<const uint8_t*>(spirv.data());
        return std::span<const uint8_t>(begin, spirv.size_bytes());
    }

    // 构建期预编译产出的字节码(见 CMake/CompilePassShader.cmake 生成的 <Name>.generated.h)按当前后端
    // 选好格式,包装成 PassShader 构造函数要的 PassShaderBytecode,vertex+fragment 或纯 compute 各一个重载
    inline PassShaderBytecode makeComputeBytecode(rhi::Backend backend, std::span<const uint32_t> spirv, std::string_view wgsl)
    {
        PassShaderBytecode bytecode;
        bytecode.codeFormat = backend == rhi::Backend::WebGPU ? rhi::ShaderCodeFormat::Wgsl : rhi::ShaderCodeFormat::Spirv;
        bytecode.compute = selectStageBytecode(backend, spirv, wgsl);
        return bytecode;
    }

    inline PassShaderBytecode makeVertexFragmentBytecode(
        rhi::Backend backend,
        std::span<const uint32_t> vertexSpirv, std::string_view vertexWgsl,
        std::span<const uint32_t> fragmentSpirv, std::string_view fragmentWgsl)
    {
        PassShaderBytecode bytecode;
        bytecode.codeFormat = backend == rhi::Backend::WebGPU ? rhi::ShaderCodeFormat::Wgsl : rhi::ShaderCodeFormat::Spirv;
        bytecode.vertex = selectStageBytecode(backend, vertexSpirv, vertexWgsl);
        bytecode.fragment = selectStageBytecode(backend, fragmentSpirv, fragmentWgsl);
        return bytecode;
    }

    // 各 Pass 的 GraphicsPipelineState 里,光栅化/深度/模板这一组字段(topology/polygonMode/
    // samples/cullMode/frontFace/depthTestEnable/depthWriteEnable/depthCompareOp/stencil*)
    // 在同一类 Pass 之间几乎完全重复,真正逐 Pass 不同的只有 shader/vertexLayout(网格类 Pass 才有)/
    // colorBlendState/attachmentFormats。这里按已经出现过的三种组合各提供一个工厂函数,调用方
    // 拿到预填好的 GraphicsPipelineState 后只需要再补 shader 和 colorBlendState/attachmentFormats。
    namespace PipelineStatePresets
    {
        // 全屏三角形合成 pass(Present/IndirectLighting/DirectLighting/Atmosphere 这类只读、不写
        // 深度的 HDR 合成 pass): Triangle_List、Fill、cull Back、CCW, 配合引擎统一的 reversed-Z
        // 约定用 Greater_Equal 做深度测试但不写深度(读已有深度做棋盘式裁剪, 不影响后续 pass 复用
        // 同一张深度附件)。
        inline GraphicsPipelineState fullscreenTriangle()
        {
            GraphicsPipelineState state;
            state.topology = static_cast<uint32_t>(rhi::PrimitiveTopology::Triangle_List);
            state.polygonMode = static_cast<uint32_t>(rhi::PolygonMode::Fill);
            state.samples = 0;
            state.cullMode = static_cast<uint32_t>(rhi::CullMode::Back);
            state.frontFace = static_cast<uint32_t>(rhi::FrontFace::Counter_Clockwise);
            state.depthTestEnable = 0;
            state.depthWriteEnable = 0;
            state.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Greater_Equal);
            state.stencilTestEnable = 0;
            state.stencilState = StencilState::get();
            state.colorBlendState = ColorBlendState::get();
            return state;
        }

        // Triangle_Strip 画的公告牌几何体(GSplat 渲染/深度这类): 可见性和排序已经由 Culling/
        // RadixSort pass 提前算好, 这里只用 reversed-Z Greater_Equal 做深度测试、不写深度。
        inline GraphicsPipelineState billboard()
        {
            GraphicsPipelineState state;
            state.topology = static_cast<uint32_t>(rhi::PrimitiveTopology::Triangle_Strip);
            state.polygonMode = static_cast<uint32_t>(rhi::PolygonMode::Fill);
            state.samples = 0;
            state.cullMode = static_cast<uint32_t>(rhi::CullMode::Back);
            state.frontFace = static_cast<uint32_t>(rhi::FrontFace::Counter_Clockwise);
            state.depthTestEnable = 1;
            state.depthWriteEnable = 0;
            state.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Greater_Equal);
            state.stencilTestEnable = 0;
            state.stencilState = StencilState::get();
            state.colorBlendState = ColorBlendState::get();
            return state;
        }

        // 写深度的不透明网格几何体(GbufferPass/CascadeShadowDepthPass): 深度读写都开, cullMode
        // 按材质的 doubleSided 属性决定。depthCompareOp 留给调用方显式传入而不是固定在预设里——
        // CascadeShadowDepthPass 用的是正向 Z 的 Less, 和其它 pass 统一使用的 reversed-Z
        // Greater_Equal 约定不一样, 预设不能替它擅自决定。
        inline GraphicsPipelineState opaqueMesh(bool doubleSided, rhi::CompareOp depthCompareOp)
        {
            GraphicsPipelineState state;
            state.topology = static_cast<uint32_t>(rhi::PrimitiveTopology::Triangle_List);
            state.polygonMode = static_cast<uint32_t>(rhi::PolygonMode::Fill);
            state.samples = 0;
            state.cullMode = static_cast<uint32_t>(doubleSided ? rhi::CullMode::None : rhi::CullMode::Back);
            state.frontFace = static_cast<uint32_t>(rhi::FrontFace::Counter_Clockwise);
            state.depthTestEnable = 1;
            state.depthWriteEnable = 1;
            state.depthCompareOp = static_cast<uint32_t>(depthCompareOp);
            state.stencilTestEnable = 0;
            state.stencilState = StencilState::get();
            state.colorBlendState = ColorBlendState::get();
            return state;
        }
    }

    // passLayout/materialLayout 都为空是合法的(比如共享 geometry-only shader 的 pipeline,
    // 完全没有 pass/材质相关的资源要绑),此时生成的 pipeline layout 只有 set0/set1,没有 set2
    inline rhi::PipelineLayout* createRenderPassPipelineLayout(
        RendererContext& rendererContext,
        const rhi::BindGroupLayout* passLayout = nullptr,
        const rhi::BindGroupLayout* materialLayout = nullptr)
    {
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::PipelineLayoutKey key;
        key.bindGroupLayouts[0] = &rendererContext.getGlobalDataBindGroupLayout();
        key.bindGroupLayouts[1] = &rendererContext.getSceneAndViewBindGroupLayout();
        key.bindGroupLayouts[2] = materialLayout != nullptr ? materialLayout : passLayout;
        key.bindGroupLayouts[3] = nullptr;
        return pipelineCache.getOrCreate(key);
    }

    inline rhi::Pipeline* createComputePipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& passLayout)
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &passLayout, nullptr);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::ComputeKey key;
        key.shader = shaderVariant;
        key.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateComputePipeline(key);
    }

    inline void setViewportAndScissor(rhi::CommandList& cmd, float4 inViewport)
    {
        cmd.setViewport(
            inViewport.x,
            inViewport.y,
            inViewport.z,
            inViewport.w,
            0.0f,
            1.0f
        );
        cmd.setScissor(
            static_cast<int32_t>(inViewport.x),
            static_cast<int32_t>(inViewport.y),
            static_cast<uint32_t>(inViewport.z),
            static_cast<uint32_t>(inViewport.w)
        );
    }
}
