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
#include <RenderBase/PassShaderUtils.h>
#include <RenderBase/RenderGraph/RenderGraph.h>

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
        // 用 getUniqueId() 而不是裸指针: 对象销毁后分配器可能把下一个新对象分配到同一个地址上,
        // 裸指针会把"新建的资源"误判成"还是原来那个没变过", 导致 needUpdate() 漏判、绑定组里
        // 留着指向已销毁资源的句柄不放。这里故意一个类型写一个重载而不是塞进一个模板, 哪天有新的
        // descriptor 类型接进 BindGroupCache 却忘了给它加 getUniqueId(), 编译期就直接报错,
        // 不会悄悄退化回裸指针比较。
        static uint64_t handleToUint64(const rhi::Buffer* buffer) noexcept { return buffer ? buffer->getUniqueId() : 0; }
        static uint64_t handleToUint64(const rhi::TextureView* textureView) noexcept { return textureView ? textureView->getUniqueId() : 0; }
        static uint64_t handleToUint64(const rhi::Sampler* sampler) noexcept { return sampler ? sampler->getUniqueId() : 0; }
        static uint64_t handleToUint64(std::nullptr_t) noexcept { return 0; }

        TRef<rhi::BindGroup> mBindGroup;
        std::array<uint64_t, HandleCount> mLastHandles{};
        std::array<uint64_t, HandleCount> mPendingHandles{};
    };

    // 配合 createPassBindGroupLayout(见 RenderBase/PassShaderUtils.h): 按 .slang 里的资源名字(而不是数字下标)写 BindGroupEntry,
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

    // 各 Pass 的 GraphicsPipelineState 里,光栅化/深度/模板这一组字段(topology/
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
    // 完全没有 pass/材质相关的资源要绑),此时生成的 pipeline layout 只有 set0/set1,没有 set2。
    // instanceLayout 是 set3, 给"顶点数据不是普通逐 instance 数据, 需要额外一份绑定才能在
    // 顶点属性 getter 里查出真正的 instanceIndex/其它每种几何各自的附加数据"这类顶点布局用
    // (目前是 Skeletal_Mesh 的 SkinningManager, 以后 spline 一类的顶点布局也走这个位置)——
    // 是不是要传、传哪个具体的 layout, 由调用方按 vertexLayout 的类型决定, 这个函数不关心
    inline rhi::PipelineLayout* createRenderPassPipelineLayout(
        RendererContext& rendererContext,
        const rhi::BindGroupLayout* passLayout = nullptr,
        const rhi::BindGroupLayout* materialLayout = nullptr,
        const rhi::BindGroupLayout* instanceLayout = nullptr)
    {
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::PipelineLayoutKey key;
        key.bindGroupLayouts[0] = &rendererContext.getGlobalDataBindGroupLayout();
        key.bindGroupLayouts[1] = &rendererContext.getViewDataManager().getBindGroupLayout();
        key.bindGroupLayouts[2] = materialLayout != nullptr ? materialLayout : passLayout;
        key.bindGroupLayouts[3] = instanceLayout;
        return pipelineCache.getOrCreateLayout(key);
    }

    inline rhi::Pipeline* createComputePipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& passLayout,
        const rhi::BindGroupLayout* instanceLayout = nullptr)
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &passLayout, nullptr, instanceLayout);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::ComputeKey key;
        key.shader = shaderVariant;
        key.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateComputePipeline(key);
    }

    // render graph 的 importTexture()/importBuffer() 不做去重——同一份物理资源被两次 import()
    // 会变成两个互不知情的独立资源节点, 各自的访问历史/自动 barrier 推导都看不到对方那一份,
    // 必须靠调用方自己保证"同一份物理资源这一帧只 import 一次"。这个帮助函数把"先查 blackboard,
    // 有就直接复用, 没有才现 import 并存回去"这个模式抽出来, 照抄 GlobalDistanceFieldVisualizerPass
    // 里 CardAtlasAlbedo 那份已有的手写模式
    template <typename Handle, typename ImportFn>
    Handle getOrImportResource(rg::RenderGraph& renderGraph, HString key, ImportFn&& importFn)
    {
        Handle handle = renderGraph.getBlackboard().get<Handle>(key);
        if (!handle.isValid())
        {
            handle = std::forward<ImportFn>(importFn)();
            renderGraph.getBlackboard().put(key, handle);
        }
        return handle;
    }

    inline void setViewportAndScissor(rhi::CommandList& cmdList, float4 inViewport)
    {
        cmdList.setViewport(
            inViewport.x,
            inViewport.y,
            inViewport.z,
            inViewport.w,
            0.0f,
            1.0f
        );
        cmdList.setScissor(
            static_cast<int32_t>(inViewport.x),
            static_cast<int32_t>(inViewport.y),
            static_cast<uint32_t>(inViewport.z),
            static_cast<uint32_t>(inViewport.w)
        );
    }
}
