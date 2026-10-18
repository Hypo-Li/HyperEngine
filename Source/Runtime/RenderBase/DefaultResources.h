#pragma once
#include <filesystem>

#include <RHI/Texture.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>
#include <Core/Common/Refable.h>
#include <Core/Math/Vector.h>

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

        // 通用占位 storage buffer: 全零填充, 约定同上面的占位 uniform buffer
        static constexpr uint32_t kDummyStorageBufferSize = 512;

        DefaultResources(RenderBackend& renderBackend, const std::filesystem::path& resourceDir);

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

        // UE FastBlueNoise_vec2_128x128x64 原始纹理提取出来的 DDS——128x128 每帧, 竖直堆叠 64
        // 帧, .rg 通道是 [0,1) 的蓝噪声二维向量, .ba 恒定未使用。尺寸是 Resource/
        // FastBlueNoise_vec2_128x128x64.dds 这份具体资产的固定值, 见 Shader/Common/
        // RadiosityProbeSampling.slang::kBlueNoiseVec2Dimensions
        rhi::TextureView& getBlueNoiseVec2View() const noexcept
        {
            return *mBlueNoiseVec2View;
        }

        rhi::Buffer& getDummyUniformBuffer() const noexcept
        {
            return *mDummyUniformBuffer;
        }

        rhi::Buffer& getDummyStorageBuffer() const noexcept
        {
            return *mDummyStorageBuffer;
        }

        // 局部光源(点光/聚光)用来限定 deferred 着色范围的包围体——LightVolume.slang 的
        // VertexMain 按 SV_VertexID 现算(用这盏灯真实的 attenuationRadius/outerConeAngle,
        // 不是烘焙单位网格再非均匀缩放近似), 这里不持有任何顶点/索引 buffer, 只留分段数常量
        // 给 CPU 侧算 drawIndexed 的 vertexCount, 改这两组值要同步改 LightVolume.slang 里的
        // kSphereSides/kSphereRings/kConeSides/kConeSlices
        static constexpr uint32_t kLightVolumeSphereSegments = 18; // 经度切分, 对应 UE 的 NumSides
        static constexpr uint32_t kLightVolumeSphereRings = 12;    // 纬度切分, 对应 UE 的 NumRings

        static constexpr uint32_t kLightVolumeConeSides = 18;  // 对应 UE FStencilConeIndexBuffer::NumSides
        static constexpr uint32_t kLightVolumeConeSlices = 12; // 对应 UE FStencilConeIndexBuffer::NumSlices

        // Visualization 用的单位线框几何——全部是 Line_List 拓扑, 顶点/索引拼在同一个 buffer 里
        // (跟 LightVolume 那两份同一个布局约定), VisualizationPass 按每个 instance 自己的形状
        // 参数(halfExtents/radius/range 等)做非均匀缩放, 不在这里区分具体实例。占位图标先用
        // 纯色块代替真实美术资源, 以后替换时只需要改 createVisualizationIconAtlas() 内部, 不影响
        // getVisualizationIconAtlasUV() 这个查表接口

        rhi::Buffer& getVisualizationWireframeBoxBuffer() const noexcept
        {
            return *mVisualizationWireframeBoxBuffer;
        }

        uint64_t getVisualizationWireframeBoxIndexOffset() const noexcept
        {
            return mVisualizationWireframeBoxIndexOffset;
        }

        uint32_t getVisualizationWireframeBoxIndexCount() const noexcept
        {
            return mVisualizationWireframeBoxIndexCount;
        }

        static constexpr uint32_t kVisualizationWireframeSphereSegments = 32; // 三个正交大圆环各自的分段数

        rhi::Buffer& getVisualizationWireframeSphereBuffer() const noexcept
        {
            return *mVisualizationWireframeSphereBuffer;
        }

        uint64_t getVisualizationWireframeSphereIndexOffset() const noexcept
        {
            return mVisualizationWireframeSphereIndexOffset;
        }

        uint32_t getVisualizationWireframeSphereIndexCount() const noexcept
        {
            return mVisualizationWireframeSphereIndexCount;
        }

        // 单位胶囊体: 圆柱段半高=1、半径=1, 两端半球盖各用两条正交半圆弧表示——跟单位圆柱/
        // 单位球共用同一套"轴向缩放 halfHeight、径向缩放 radius"的非均匀缩放假设, 两端半球盖
        // 半径恒等于径向缩放后的 radius, 不需要额外参数
        static constexpr uint32_t kVisualizationWireframeCapsuleSegments = 16; // 每条半圆弧的分段数

        rhi::Buffer& getVisualizationWireframeCapsuleBuffer() const noexcept
        {
            return *mVisualizationWireframeCapsuleBuffer;
        }

        uint64_t getVisualizationWireframeCapsuleIndexOffset() const noexcept
        {
            return mVisualizationWireframeCapsuleIndexOffset;
        }

        uint32_t getVisualizationWireframeCapsuleIndexCount() const noexcept
        {
            return mVisualizationWireframeCapsuleIndexCount;
        }

        // 单位圆柱: 半高=1、半径=1, 轴向 +Z, 顶/底两圈圆环 + 若干条纵向连接线
        static constexpr uint32_t kVisualizationWireframeCylinderSegments = 24;
        static constexpr uint32_t kVisualizationWireframeCylinderSideLines = 4;

        rhi::Buffer& getVisualizationWireframeCylinderBuffer() const noexcept
        {
            return *mVisualizationWireframeCylinderBuffer;
        }

        uint64_t getVisualizationWireframeCylinderIndexOffset() const noexcept
        {
            return mVisualizationWireframeCylinderIndexOffset;
        }

        uint32_t getVisualizationWireframeCylinderIndexCount() const noexcept
        {
            return mVisualizationWireframeCylinderIndexCount;
        }

        // 单位圆锥: 锥顶在原点, 轴向 -Z(跟聚光灯锥轴约定一致), 烘焙在 45 度参考角、range=1——形状跟 LightVolume.slang
        // 的 computeConeLocalPosition() 同一套构造(侧面终止于 zRadius=range*cos(halfAngle) 处
        // 的边界圆, 之外是球心在锥顶、半径 range 的球面被半角裁出的那顶球冠, 不是简化成平底
        // 圆盘), 只是这里没有逐实例现算, 仍然是烘焙单位网格 + 非均匀缩放(轴向缩放 range, 径向
        // 缩放 range*tan(实际半角))去近似任意半角, 偏离 45° 参考角越多越不精确——这只是编辑器
        // 辅助线, 不影响实际光照, 可以接受这个近似
        static constexpr uint32_t kVisualizationWireframeLightConeSides = 24; // 放射线/边界圆的分段数
        static constexpr uint32_t kVisualizationWireframeLightConeCapSegments = 8; // 球冠每条弧的分段数

        rhi::Buffer& getVisualizationWireframeLightConeBuffer() const noexcept
        {
            return *mVisualizationWireframeLightConeBuffer;
        }

        uint64_t getVisualizationWireframeLightConeIndexOffset() const noexcept
        {
            return mVisualizationWireframeLightConeIndexOffset;
        }

        uint32_t getVisualizationWireframeLightConeIndexCount() const noexcept
        {
            return mVisualizationWireframeLightConeIndexCount;
        }

        // 占位图标 atlas——按 VisualizationIconType 的声明顺序切成等宽的一排格子, 每格一种纯色,
        // 真实美术资源接入前先用这个占位。VisualizationIconType 本身声明在 Renderer/VisualizationItems.h,
        // DefaultResources 属于更底层的 RenderBase, 不能反过来 include Renderer 的头——这里只
        // 按"第几个格子"这个裸索引查, 由调用方(VisualizationPass)自己知道枚举值和格子序号的对应关系
        static constexpr uint32_t kVisualizationIconAtlasCellCount = 4;
        static constexpr uint32_t kVisualizationIconAtlasCellSize = 64;

        rhi::TextureView& getVisualizationIconAtlasView() const noexcept
        {
            return *mVisualizationIconAtlasView;
        }

        // 返回第 cellIndex 个图标格子的 UV 矩形(u0, v0, u1, v1)
        float4 getVisualizationIconAtlasUV(uint32_t cellIndex) const noexcept;

    private:
        void createSolidColorTexture(rhi::CommandList& cmdList, const uint8_t (&color)[4], TRef<rhi::Texture>& outTexture, TRef<rhi::TextureView>& outView);

        void createBlackTexture3D(rhi::CommandList& cmdList);

        void createBlackTextureCube(rhi::CommandList& cmdList);

        void createDummyUniformBuffer(rhi::CommandList& cmdList);
        void createDummyStorageBuffer(rhi::CommandList& cmdList);

        void createDfgLutTexture(rhi::CommandList& cmdList);

        void createBlueNoiseVec2Texture(rhi::CommandList& cmdList);

        void createVisualizationWireframeBoxGeometry(rhi::CommandList& cmdList);

        void createVisualizationWireframeSphereGeometry(rhi::CommandList& cmdList);

        void createVisualizationWireframeCapsuleGeometry(rhi::CommandList& cmdList);

        void createVisualizationWireframeCylinderGeometry(rhi::CommandList& cmdList);

        void createVisualizationWireframeLightConeGeometry(rhi::CommandList& cmdList);

        void createVisualizationIconAtlas(rhi::CommandList& cmdList);

    private:
        RenderBackend& mRenderBackend;
        std::filesystem::path mResourceDir;
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
        TRef<rhi::Texture> mBlueNoiseVec2Texture;
        TRef<rhi::TextureView> mBlueNoiseVec2View;
        TRef<rhi::Buffer> mDummyUniformBuffer;
        TRef<rhi::Buffer> mDummyStorageBuffer;

        TRef<rhi::Buffer> mVisualizationWireframeBoxBuffer;
        uint64_t mVisualizationWireframeBoxIndexOffset = 0;
        uint32_t mVisualizationWireframeBoxIndexCount = 0;

        TRef<rhi::Buffer> mVisualizationWireframeSphereBuffer;
        uint64_t mVisualizationWireframeSphereIndexOffset = 0;
        uint32_t mVisualizationWireframeSphereIndexCount = 0;

        TRef<rhi::Buffer> mVisualizationWireframeCapsuleBuffer;
        uint64_t mVisualizationWireframeCapsuleIndexOffset = 0;
        uint32_t mVisualizationWireframeCapsuleIndexCount = 0;

        TRef<rhi::Buffer> mVisualizationWireframeCylinderBuffer;
        uint64_t mVisualizationWireframeCylinderIndexOffset = 0;
        uint32_t mVisualizationWireframeCylinderIndexCount = 0;

        TRef<rhi::Buffer> mVisualizationWireframeLightConeBuffer;
        uint64_t mVisualizationWireframeLightConeIndexOffset = 0;
        uint32_t mVisualizationWireframeLightConeIndexCount = 0;

        TRef<rhi::Texture> mVisualizationIconAtlasTexture;
        TRef<rhi::TextureView> mVisualizationIconAtlasView;
    };
}
