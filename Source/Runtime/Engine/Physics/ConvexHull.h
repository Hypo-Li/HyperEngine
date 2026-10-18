#pragma once
#include <Core/Object/Object.h>
#include <Core/Common/BulkData.h>
#include <Core/Math/Vector.h>

#include <RenderBase/ProxyHandle.h>

#include <span>
#include <vector>

namespace hyp
{
    // 存的点不需要就是凸包本身的顶点, 可以含内部点。
    class ConvexHull : public Object
    {
        REFLECT_CLASS(ConvexHull)
    public:
        ConvexHull() = default;

        ~ConvexHull() noexcept override;

        // 跟 Jolt 的 cDefaultConvexRadius 对齐。
        float getConvexRadius() const noexcept
        {
            return mConvexRadius;
        }

        void setConvexRadius(float convexRadius) noexcept
        {
            mConvexRadius = convexRadius;
        }

        // 纯元数据, 不触发点数据加载。
        uint32_t getPointCount() const noexcept
        {
            return static_cast<uint32_t>(mPointData.getSize() / sizeof(float3));
        }

        void setPoints(std::vector<float3> points);

        // fn 结束后如果调用前是 Unloaded 会自动还原。
        template <typename F>
        void accessPoints(F&& fn) const
        {
            bool wasAlreadyLoaded = mPointData.isLoaded();
            if (!wasAlreadyLoaded)
                const_cast<BulkData&>(mPointData).load();

            std::span<const uint8_t> bytes = mPointData.getData();
            fn(std::span<const float3>(reinterpret_cast<const float3*>(bytes.data()), bytes.size() / sizeof(float3)));

            if (!wasAlreadyLoaded)
                const_cast<BulkData&>(mPointData).unload();
        }

        // 目前只给可视化系统用: 内部临时构造一次 Jolt 的 JPH::ConvexHullShape 来读取算好的
        // 凸包面/顶点(不是这份点云本身, mPointData 可能含内部点), 三角化后建一份持久的
        // GeometryProxy, 跟 Mesh::getOrCreateGeometryProxy() 一样是懒创建、句柄自己持有的
        // 模式。这个临时 Shape 用完即弃, 跟 Play 模式下真正的物理 Body 建不建无关, 也不会
        // 有 Jolt 类型跨出 ConvexHull.cpp
        GeometryProxyHandle getOrCreateGeometryProxy();

    private:
        void createGeometryProxy(GeometryProxyHandle handle);

        void invalidateGeometryProxy() noexcept;

        BulkData mPointData;
        float mConvexRadius = 0.05f;
        GeometryProxyHandle mGeometryProxy;
    };

    template <> Type* Reflection::createType<ConvexHull>();
}
