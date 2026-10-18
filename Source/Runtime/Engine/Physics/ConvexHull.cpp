#include "ConvexHull.h"
#include "../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <RenderBase/GeometryProxy.h>
#include <RenderBase/VertexLayout.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

#include <cstring>

namespace hyp
{
    REGISTER_CLASS(ConvexHull)

    ConvexHull::~ConvexHull() noexcept
    {
        invalidateGeometryProxy();
    }

    void ConvexHull::setPoints(std::vector<float3> points)
    {
        std::vector<uint8_t> bytes(points.size() * sizeof(float3));
        std::memcpy(bytes.data(), points.data(), bytes.size());
        mPointData.setData(std::move(bytes));

        // 点变了, 之前缓存的可视化几何(如果建过)已经过期。
        invalidateGeometryProxy();
    }

    GeometryProxyHandle ConvexHull::getOrCreateGeometryProxy()
    {
        if (!mGeometryProxy.isValid())
        {
            mGeometryProxy = Context::get().getRenderFrontend().allocateProxyHandle<ProxyType::Geometry>();
            createGeometryProxy(mGeometryProxy);
        }

        return mGeometryProxy;
    }

    void ConvexHull::createGeometryProxy(GeometryProxyHandle handle)
    {
        // 用 mPointData 现有的点临时构造一次 JPH::ConvexHullShape——这一步只是为了让 Jolt
        // 把凸包算出来, 读完面/顶点这个 Shape 就丢, 不持有, 不跨出这个函数
        std::vector<JPH::Vec3> joltPoints;
        accessPoints(
            [&joltPoints](std::span<const float3> points)
            {
                joltPoints.reserve(points.size());
                for (const float3& p : points)
                    joltPoints.emplace_back(p.x, p.y, p.z);
            }
        );

        JPH::ConvexHullShapeSettings settings(joltPoints.data(), static_cast<int>(joltPoints.size()), mConvexRadius);
        JPH::Shape::ShapeResult result = settings.Create();
        assert(result.IsValid() && "ConvexHull::createGeometryProxy(): 凸包构造失败");
        const auto* hullShape = static_cast<const JPH::ConvexHullShape*>(result.Get().GetPtr());

        std::vector<float3> vertices;
        vertices.reserve(hullShape->GetNumPoints());
        for (uint32_t i = 0; i < hullShape->GetNumPoints(); ++i)
        {
            JPH::Vec3 p = hullShape->GetPoint(i);
            vertices.emplace_back(p.GetX(), p.GetY(), p.GetZ());
        }

        // 每个面按第一个顶点做扇形三角化——GetFaceVertices() 已经保证同一个面的顶点是
        // 按逆时针排好序的, 不需要自己重新排序
        std::vector<uint32_t> indices;
        for (uint32_t faceIndex = 0; faceIndex < hullShape->GetNumFaces(); ++faceIndex)
        {
            uint32_t vertexCountInFace = hullShape->GetNumVerticesInFace(faceIndex);
            std::vector<uint32_t> faceVertices(vertexCountInFace);
            hullShape->GetFaceVertices(faceIndex, vertexCountInFace, faceVertices.data());
            for (uint32_t i = 1; i + 1 < vertexCountInFace; ++i)
            {
                indices.push_back(faceVertices[0]);
                indices.push_back(faceVertices[i]);
                indices.push_back(faceVertices[i + 1]);
            }
        }

        std::vector<GeometryProxy::Section> sections(1);
        sections[0].vertexOffset = 0;
        sections[0].firstIndex = 0;
        sections[0].indexCount = static_cast<uint32_t>(indices.size());

        std::vector<uint8_t> vertexData(vertices.size() * sizeof(float3));
        std::memcpy(vertexData.data(), vertices.data(), vertexData.size());
        std::vector<uint8_t> indexData(indices.size() * sizeof(uint32_t));
        std::memcpy(indexData.data(), indices.data(), indexData.size());

        Context::get().getRenderFrontend().enqueueLambda(
            [handle,
            vertexData = std::move(vertexData),
            indexData = std::move(indexData),
            sections = std::move(sections)](FrameContext& context) mutable
            {
                context.getProxyManager().createProxy(
                    handle,
                    VertexLayout(VertexLayoutType::Static_Mesh, VertexAttributeMask::Position),
                    std::move(vertexData),
                    std::vector<uint64_t>{ 0 }, // 只有 Position 一条流, 起始偏移为 0
                    std::move(indexData),
                    rhi::IndexType::Uint32,
                    std::move(sections)
                );
            }
        );
    }

    void ConvexHull::invalidateGeometryProxy() noexcept
    {
        if (mGeometryProxy.isValid())
        {
            GeometryProxyHandle handle = mGeometryProxy;
            Context::get().getRenderFrontend().enqueueLambda(
                [handle](FrameContext& context)
                {
                    context.getProxyManager().destroyProxy(handle);
                }
            );
            Context::get().getRenderFrontend().freeProxyHandle(handle);
            mGeometryProxy.reset();
        }
    }

    template <> Type* Reflection::createType<ConvexHull>()
    {
        auto c = new TClass<ConvexHull>("ConvexHull");
        c->setDisplayName("Convex Hull");
        c->addProperty("Points", &ConvexHull::mPointData);
        c->addProperty("Convex Radius", &ConvexHull::mConvexRadius);
        return c;
    }
}
