#include "VisualizationEmitter.h"

namespace hyp
{
    void VisualizationEmitter::drawIconBillboard(double3 position, VisualizationIconType type, float size, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationIcon& icon = mItems.icons.emplace_back();
        icon.position = position;
        icon.type = type;
        icon.size = size;
        icon.color = color;
        icon.pickId = pickId;
        icon.ignoreDepth = ignoreDepth;
    }

    void VisualizationEmitter::drawPoint(double3 position, float size, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationPoint& point = mItems.points.emplace_back();
        point.position = position;
        point.size = size;
        point.color = color;
        point.pickId = pickId;
        point.ignoreDepth = ignoreDepth;
    }

    void VisualizationEmitter::drawLine(double3 start, double3 end, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationLine& line = mItems.lines.emplace_back();
        line.start = start;
        line.end = end;
        line.color = color;
        line.pickId = pickId;
        line.ignoreDepth = ignoreDepth;
    }

    void VisualizationEmitter::drawWireframeBox(const double4x4& transform, float3 halfExtents, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationWireframeBox& box = mItems.wireframeBoxes.emplace_back();
        box.transform = transform;
        box.halfExtents = halfExtents;
        box.color = color;
        box.pickId = pickId;
        box.ignoreDepth = ignoreDepth;
    }

    void VisualizationEmitter::drawWireframeSphere(const double4x4& transform, float radius, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationWireframeSphere& sphere = mItems.wireframeSpheres.emplace_back();
        sphere.transform = transform;
        sphere.radius = radius;
        sphere.color = color;
        sphere.pickId = pickId;
        sphere.ignoreDepth = ignoreDepth;
    }

    void VisualizationEmitter::drawWireframeCapsule(const double4x4& transform, float radius, float halfHeight, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationWireframeCapsule& capsule = mItems.wireframeCapsules.emplace_back();
        capsule.transform = transform;
        capsule.radius = radius;
        capsule.halfHeight = halfHeight;
        capsule.color = color;
        capsule.pickId = pickId;
        capsule.ignoreDepth = ignoreDepth;
    }

    void VisualizationEmitter::drawWireframeCylinder(const double4x4& transform, float radius, float halfHeight, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationWireframeCylinder& cylinder = mItems.wireframeCylinders.emplace_back();
        cylinder.transform = transform;
        cylinder.radius = radius;
        cylinder.halfHeight = halfHeight;
        cylinder.color = color;
        cylinder.pickId = pickId;
        cylinder.ignoreDepth = ignoreDepth;
    }

    void VisualizationEmitter::drawWireframeLightCone(const double4x4& transform, float range, float halfAngle, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationWireframeLightCone& cone = mItems.wireframeLightCones.emplace_back();
        cone.transform = transform;
        cone.range = range;
        cone.halfAngle = halfAngle;
        cone.color = color;
        cone.pickId = pickId;
        cone.ignoreDepth = ignoreDepth;
    }

    void VisualizationEmitter::drawWireframeGeometry(GeometryProxyHandle geometry, const double4x4& transform, float4 color, uint32_t pickId, bool ignoreDepth)
    {
        VisualizationWireframeGeometry& item = mItems.wireframeGeometries.emplace_back();
        item.geometry = geometry;
        item.transform = transform;
        item.color = color;
        item.pickId = pickId;
        item.ignoreDepth = ignoreDepth;
    }
}
