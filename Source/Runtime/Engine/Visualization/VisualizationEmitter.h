#pragma once
#include <Renderer/VisualizationItems.h>

namespace hyp
{
    // Editor(或者以后任何 gameplay 代码)每帧现造的立即模式绘制列表——只负责收集数据,
    // 不碰渲染线程、不知道 VisualizationPass/PickPass 存不存在。生命周期是纯局部的: 每帧
    // 现造一个空实例、调用方往里塞条目、结束时整体 move 给渲染线程, 没有任何跨帧状态,
    // 不需要 clear()
    class VisualizationEmitter
    {
    public:
        void drawIconBillboard(double3 position, VisualizationIconType type, float size, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        void drawPoint(double3 position, float size, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        void drawLine(double3 start, double3 end, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        void drawWireframeBox(const double4x4& transform, float3 halfExtents, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        void drawWireframeSphere(const double4x4& transform, float radius, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        void drawWireframeCapsule(const double4x4& transform, float radius, float halfHeight, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        void drawWireframeCylinder(const double4x4& transform, float radius, float halfHeight, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        void drawWireframeLightCone(const double4x4& transform, float range, float halfAngle, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        // geometry 由调用方自己通过资源的 getOrCreateGeometryProxy() 拿到(比如
        // ConvexHull::getOrCreateGeometryProxy()、Mesh::getOrCreateGeometryProxy()),
        // VisualizationEmitter 只管把它当一个不透明句柄搬运过去
        void drawWireframeGeometry(GeometryProxyHandle geometry, const double4x4& transform, float4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, uint32_t pickId = 0, bool ignoreDepth = false);

        VisualizationItems&& takeItems() noexcept
        {
            return std::move(mItems);
        }

    private:
        VisualizationItems mItems;
    };
}
