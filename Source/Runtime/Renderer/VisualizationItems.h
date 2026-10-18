#pragma once
#include <RenderBase/ProxyHandle.h>
#include <RenderBase/RenderBaseDefines.h>

#include <Core/Math/Vector.h>

#include <vector>

namespace hyp
{
    enum class VisualizationIconType : uint32_t
    {
        Light,
        Camera,
        SkyLight,
        Joint,
    };

    // color/pickId/ignoreDepth 是每种可视化图元都要带的公共字段, 抽出来避免每个 struct
    // 重复写三遍。pickId 默认 0 表示不可拾取, 由 Editor 侧决定要不要填一个真实值
    struct VisualizationItemBase
    {
        float4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        uint32_t pickId = 0;
        bool ignoreDepth = false;
    };

    struct VisualizationIcon : VisualizationItemBase
    {
        double3 position;
        VisualizationIconType type;
        float size = 1.0f;
    };

    struct VisualizationPoint : VisualizationItemBase
    {
        double3 position;
        float size = 1.0f;
    };

    struct VisualizationLine : VisualizationItemBase
    {
        double3 start;
        double3 end;
    };

    struct VisualizationWireframeBox : VisualizationItemBase
    {
        double4x4 transform;
        float3 halfExtents{ 0.5f, 0.5f, 0.5f };
    };

    struct VisualizationWireframeSphere : VisualizationItemBase
    {
        double4x4 transform;
        float radius = 0.5f;
    };

    struct VisualizationWireframeCapsule : VisualizationItemBase
    {
        double4x4 transform;
        float radius = 0.5f;
        float halfHeight = 0.5f; // 不含两端半球的那一段圆柱体半高
    };

    struct VisualizationWireframeCylinder : VisualizationItemBase
    {
        double4x4 transform;
        float radius = 0.5f;
        float halfHeight = 0.5f;
    };

    // 锥形光源的可视化惯例形状(放射线 + 边界圆 + 两条正交球冠弧), 不是通用几何锥体的线框——
    // 形状照抄 LightVolume.slang 的 computeConeLocalPosition(), 但这里是烘焙单位网格 + 非均匀
    // 缩放近似任意半角, 不是逐实例现算
    struct VisualizationWireframeLightCone : VisualizationItemBase
    {
        double4x4 transform; // 锥顶在原点, 轴向沿 -Z(跟聚光灯锥轴约定一致)
        float range = 1.0f;
        float halfAngle = 0.5f; // 弧度
    };

    // geometry 由调用方通过资源自己的 getOrCreateGeometryProxy() 拿到(比如
    // ConvexHull::getOrCreateGeometryProxy()、Mesh::getOrCreateGeometryProxy()),
    // Visualization 这一层不负责创建/缓存几何本身。不叫 XxxMesh 是因为凸包和网格都走这一个
    // 类型, 不是网格专属
    struct VisualizationWireframeGeometry : VisualizationItemBase
    {
        GeometryProxyHandle geometry;
        double4x4 transform;
    };

    // 一帧攒出来的全部可视化条目, 按图元种类分组存, 不是一个异构列表——每种类型对应
    // 渲染线程固定的一套顶点展开/管线, 分开存直接对应"每种各画一次 instanced draw"
    struct VisualizationItems
    {
        std::vector<VisualizationIcon> icons;
        std::vector<VisualizationPoint> points;
        std::vector<VisualizationLine> lines;
        std::vector<VisualizationWireframeBox> wireframeBoxes;
        std::vector<VisualizationWireframeSphere> wireframeSpheres;
        std::vector<VisualizationWireframeCapsule> wireframeCapsules;
        std::vector<VisualizationWireframeCylinder> wireframeCylinders;
        std::vector<VisualizationWireframeLightCone> wireframeLightCones;
        std::vector<VisualizationWireframeGeometry> wireframeGeometries;

        bool empty() const noexcept
        {
            return icons.empty() &&
                points.empty() &&
                lines.empty() &&
                wireframeBoxes.empty() &&
                wireframeSpheres.empty() &&
                wireframeCapsules.empty() &&
                wireframeCylinders.empty() &&
                wireframeLightCones.empty() &&
                wireframeGeometries.empty();
        }
    };
}
