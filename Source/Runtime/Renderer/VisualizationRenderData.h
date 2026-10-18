#pragma once
#include "VisualizationItems.h"
#include "RenderPass/RenderPassUtils.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

#include <span>
#include <vector>

namespace hyp
{
    class GeometryProxy;

    // offset 是元素下标而不是字节偏移, 因为 WGSL storage buffer 没有字节寻址原语
    struct VisualizationTypeRange
    {
        uint32_t offset = 0;
        uint32_t count = 0;
    };

    // geometry 存裸指针而非 GeometryProxyHandle, 因为分组这一步已经在渲染线程上执行
    struct VisualizationGeometryGroup
    {
        GeometryProxy* geometry = nullptr;
        VisualizationTypeRange range;
    };

    // 跟 VisualizationItems.h 同名的 CPU 类型不是同一份东西——这里已经转换成 WorldCoord/WorldMatrix 编码。
    // glm 的 float3/float4 在这个项目里 alignof 都是 4(没开 GLM_FORCE_ALIGNED_GENTYPES), 而 shader
    // 里的 vec3/vec4 按 std430/WGSL 规则要求 16 字节对齐——编译器不会替我们插入这些 GPU 独有的
    // 对齐空隙, 所以每个成员前后需要多少字节空隙都是手动数出来、显式用 _padN 占位的, 不能重排字段
    // 省事, 也不能删掉看起来"多余"的 _padN。每个结构体末尾的 static_assert 锁死总大小, 防止以后
    // 加字段时悄悄破坏这个手算出来的布局
    struct VisualizationIconInstance
    {
        WorldCoord position;
        float size;
        float _pad0[3];
        float4 atlasUV;
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad1[2];
    };
    static_assert(sizeof(VisualizationIconInstance) == 96);

    struct VisualizationPointInstance
    {
        WorldCoord position;
        float size;
        float _pad0[3];
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad1[2];
    };
    static_assert(sizeof(VisualizationPointInstance) == 80);

    struct VisualizationLineInstance
    {
        WorldCoord start;
        WorldCoord end;
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad0[2];
    };
    static_assert(sizeof(VisualizationLineInstance) == 96);

    struct VisualizationWireframeBoxInstance
    {
        WorldMatrix transform;
        float3 halfExtents;
        float _pad0[1];
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad1[2];
    };
    static_assert(sizeof(VisualizationWireframeBoxInstance) == 112);

    struct VisualizationWireframeSphereInstance
    {
        WorldMatrix transform;
        float radius;
        float _pad0[3];
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad1[2];
    };
    static_assert(sizeof(VisualizationWireframeSphereInstance) == 112);

    struct VisualizationWireframeCapsuleInstance
    {
        WorldMatrix transform;
        float radius;
        float halfHeight;
        float _pad0[2];
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad1[2];
    };
    static_assert(sizeof(VisualizationWireframeCapsuleInstance) == 112);

    struct VisualizationWireframeCylinderInstance
    {
        WorldMatrix transform;
        float radius;
        float halfHeight;
        float _pad0[2];
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad1[2];
    };
    static_assert(sizeof(VisualizationWireframeCylinderInstance) == 112);

    struct VisualizationWireframeLightConeInstance
    {
        WorldMatrix transform;
        float range;
        float halfAngle;
        float _pad0[2];
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad1[2];
    };
    static_assert(sizeof(VisualizationWireframeLightConeInstance) == 112);

    // geometry 句柄不进这个结构体, 整组共用, 靠 bind 一次
    struct VisualizationWireframeGeometryInstance
    {
        WorldMatrix transform;
        float4 color;
        uint32_t pickId;
        uint32_t ignoreDepth;
        uint32_t _pad0[2];
    };
    static_assert(sizeof(VisualizationWireframeGeometryInstance) == 96);

    // layout 是 set2 的 BindGroupLayout, 和 pipeline->getLayout() 返回的 PipelineLayout 不是同一个类型
    struct VisualizationShapePipelines
    {
        struct Entry
        {
            rhi::Pipeline* pipeline = nullptr;
            const rhi::BindGroupLayout* layout = nullptr;
        };

        Entry icon;
        Entry point;
        Entry line;
        Entry wireframeBox;
        Entry wireframeSphere;
        Entry wireframeCapsule;
        Entry wireframeCylinder;
        Entry wireframeLightCone;
        Entry wireframeGeometry;
    };

    // 必须由调用方通过 RendererContext::getPassStorage() 持久持有, 不能是 VisualizationRenderData 的成员
    struct VisualizationShapeBindGroupCaches
    {
        BindGroupCache<1> icon;
        BindGroupCache<1> point;
        BindGroupCache<1> line;
        BindGroupCache<1> wireframeBox;
        BindGroupCache<1> wireframeSphere;
        BindGroupCache<1> wireframeCapsule;
        BindGroupCache<1> wireframeCylinder;
        BindGroupCache<1> wireframeLightCone;
        // 每个 GeometryProxy Group 的 bind group 都要多绑一份专属于它自己的 geometry buffer
        // (见 VisualizationRenderData::draw() 里 wireframeGeometry 那段的注释), 不能像其余 8 种
        // 形状那样全场景共用一份缓存——按 mGeometryGroups 的下标 resize-on-demand
        std::vector<BindGroupCache<2>> wireframeGeometry;
    };

    // 纯值类型, 不持有任何跨帧状态
    class VisualizationRenderData
    {
    public:
        // 只应该每帧调用一次——不做重复调用防护, 由调用方自己保证。cameraWorldPos 用来给
        // ignoreDepth(AlwaysOnTop)条目排序, 见 build() 内部的排序说明
        static VisualizationRenderData build(rg::RenderGraph& renderGraph, RendererContext& rendererContext, const VisualizationItems& items, const double3& cameraWorldPos);

        bool empty() const noexcept;

        rg::BufferHandle getBuffer() const noexcept { return mBuffer; }

        // wireframeGeometry 引用的 GeometryProxy 只有在这一帧被真正当可渲染网格画出来时才会有
        // 别的代码替它调 prepare()(见 SceneRenderer.cpp 按 data.geometries 调用的那段)——纯碰撞、
        // 没有配套 MeshRenderer 的 GeometryProxy 不会被那条路径碰到, 顶点/索引数据可能从来没有
        // flush 上 GPU。调用方要在这个 pass 的 beginPass() 之前对每个 group 调一次 geometry->
        // prepare(cmdList)(prepare() 内部会插 barrier, render pass 内部不能插 barrier)
        std::span<const VisualizationGeometryGroup> getGeometryGroups() const noexcept { return mGeometryGroups; }

        // 对每个非空类型换管线、按需更新 BindGroup、push constant 传 offset 后发一次 instanced draw
        void draw(
            rg::ExecuteContext& executeContext,
            RendererContext& rendererContext,
            VisualizationShapeBindGroupCaches& bindGroupCaches,
            const VisualizationShapePipelines& pipelines
        ) const;

    private:
        rg::BufferHandle mBuffer;
        uint64_t mBufferByteSize = 0;

        VisualizationTypeRange mIcons;
        VisualizationTypeRange mPoints;
        VisualizationTypeRange mLines;
        VisualizationTypeRange mWireframeBoxes;
        VisualizationTypeRange mWireframeSpheres;
        VisualizationTypeRange mWireframeCapsules;
        VisualizationTypeRange mWireframeCylinders;
        VisualizationTypeRange mWireframeLightCones;
        std::vector<VisualizationGeometryGroup> mGeometryGroups;
    };
}
