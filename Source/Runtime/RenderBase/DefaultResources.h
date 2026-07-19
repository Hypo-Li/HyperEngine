#pragma once
#include <RHI/Texture.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>
#include <Core/Common/Refable.h>

namespace hyp
{
    class RenderBackend;

    // 引擎内部常用的占位 GPU 资源(1x1 黑/白纹理、DFG LUT 这类"固定不变、任何场景都可能用得上"的
    // 资源)统一在这里懒加载一次, 避免每个 Pass 各自维护一份几乎一样的"首次使用时创建"样板代码。
    // ensureCreated() 由 FrameContext::render() 在每帧最开始、command list 刚 reset 完、还没有
    // 任何 render/compute pass encoder 活跃时调用一次(第一帧真正创建, 之后直接跳过), 这个时机
    // 天然满足"barrier 不能在 pass 内插入"的约束, 不需要像 Pass 自己懒加载那样单独包一个
    // RenderGraph TransferPass。
    class DefaultResources
    {
    public:
        // 通用占位 uniform buffer: 全零填充, 大小取够用的整数倍, 谁需要占位 uniform buffer 就用
        // PassBindGroupWriter::setBuffer(name, buffer, 0, sizeof(SomeStruct)) 从这一份 buffer
        // 里截自己需要的大小, 不必每种 struct 各开一个专用 buffer。DefaultResources 不认识、也不
        // 应该认识任何具体业务 struct 的布局——如果某个 struct 的 shader 端数学不能接受全零输入
        // (比如半径为零), 应该由消费方的 shader 自己判断字段是否退化并跳过相应计算, 而不是让这里
        // 塞一份"看起来安全"的非零默认值
        static constexpr uint32_t kDummyUniformBufferSize = 512;

        explicit DefaultResources(RenderBackend& renderBackend);

        // 第一次调用时创建全部占位资源并记录上传命令, 之后直接返回。必须在 cmdList 未处于任何
        // render/compute pass 内时调用(FrameContext::render() 里 cmdList.reset() 之后、
        // mRenderGraph->execute() 之前是理想位置)
        void ensureCreated(rhi::CommandList& cmdList);

        rhi::TextureView& getWhiteTextureView() const noexcept
        {
            return *mWhiteTextureView;
        }

        rhi::TextureView& getBlackTextureView() const noexcept
        {
            return *mBlackTextureView;
        }

        rhi::TextureView& getBlackTexture3DView() const noexcept
        {
            return *mBlackTexture3DView;
        }

        rhi::TextureView& getBlackTextureCubeView() const noexcept
        {
            return *mBlackTextureCubeView;
        }

        rhi::TextureView& getDfgLutView() const noexcept
        {
            return *mDfgLutView;
        }

        rhi::Buffer& getDummyUniformBuffer() const noexcept
        {
            return *mDummyUniformBuffer;
        }

    private:
        void createSolidColorTexture(rhi::CommandList& cmdList, const uint8_t (&color)[4], TRef<rhi::Texture>& outTexture, TRef<rhi::TextureView>& outView);

        void createBlackTexture3D(rhi::CommandList& cmdList);

        void createBlackTextureCube(rhi::CommandList& cmdList);

        void createDummyUniformBuffer(rhi::CommandList& cmdList);

        void createDfgLutTexture(rhi::CommandList& cmdList);

    private:
        RenderBackend& mRenderBackend;
        bool mCreated = false;

        TRef<rhi::Texture> mWhiteTexture;
        TRef<rhi::TextureView> mWhiteTextureView;
        TRef<rhi::Texture> mBlackTexture;
        TRef<rhi::TextureView> mBlackTextureView;
        TRef<rhi::Texture> mBlackTexture3D;
        TRef<rhi::TextureView> mBlackTexture3DView;
        TRef<rhi::Texture> mBlackTextureCube;
        TRef<rhi::TextureView> mBlackTextureCubeView;
        TRef<rhi::Texture> mDfgLutTexture;
        TRef<rhi::TextureView> mDfgLutView;
        TRef<rhi::Buffer> mDummyUniformBuffer;
    };
}
