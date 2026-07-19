#include "Mesh.h"

#include <array>

#include <RenderBase/RenderFrontend.h>
#include <RenderBase/GeometryProxy.h>
#include "../Context.h"

namespace hyp
{
    template <> Type* Reflection::createType<VertexAttributeType>()
    {
        auto e = new TEnumeration<VertexAttributeType>("VertexAttributeType");
        e->addValue("Position", VertexAttributeType::Position);
        e->addValue("Normal", VertexAttributeType::Normal);
        e->addValue("Tangent", VertexAttributeType::Tangent);
        e->addValue("Color", VertexAttributeType::Color);
        e->addValue("TexCoord", VertexAttributeType::TexCoord);
        e->addValue("Bone", VertexAttributeType::Bone);
        e->addValue("Weight", VertexAttributeType::Weight);
        return e;
    }

    template <> Type* Reflection::createType<VertexAttribute>()
    {
        auto s = new TStructure<VertexAttribute>("VertexAttribute");
        s->addProperty("Type", &VertexAttribute::type);
        s->addProperty("Stride", &VertexAttribute::stride);
        return s;
    }

    template <> Type* Reflection::createType<IndexType>()
    {
        auto e = new TEnumeration<IndexType>("IndexType");
        e->addValue("Uint16", IndexType::Uint16);
        e->addValue("Uint32", IndexType::Uint32);
        return e;
    }

    REGISTER_CLASS(Mesh)

    Mesh::Mesh()
    {

    }

    Mesh::~Mesh() noexcept
    {
        // 和 Texture::~Texture()/Material::~Material() 一样, 只在真的创建过 proxy 时才碰
        // Context::get()——Mesh 从没调用过 getOrCreateGeometryProxy() 就被析构(比如纯粹
        // 在内存里构造/转换资产, 不经过渲染)是合法场景, 这时候 Context 可能压根没初始化过
        for (auto& geometryProxy : mLODGeometryProxies)
        {
            if (geometryProxy.isValid())
            {
                destroyGeometryProxy(geometryProxy);
                Context::get().getRenderFrontend().freeProxyHandle(geometryProxy);
                geometryProxy.reset();
            }
        }
    }

    uint32_t Mesh::addLOD()
    {
        mLODs.emplace_back();
        return static_cast<uint32_t>(mLODs.size() - 1);
    }

    void Mesh::setLODData(
        uint32_t lodLevel,
        std::vector<uint8_t> vertexData,
        std::vector<VertexAttribute> vertexAttributes,
        std::vector<uint8_t> indexData,
        IndexType indexType,
        float minScreenPercentage
    )
    {
        assert(lodLevel < mLODs.size());
        LOD& lod = mLODs[lodLevel];
        lod.vertexData.setData(std::move(vertexData));
        lod.vertexAttributes = std::move(vertexAttributes);
        lod.indexData.setData(std::move(indexData));
        lod.indexType = indexType;
        lod.minScreenPercentage = minScreenPercentage;
    }

    void Mesh::addSubmesh(
        uint32_t lodLevel,
        uint32_t vertexOffset,
        uint32_t vertexCount,
        uint32_t indexOffset,
        uint32_t indexCount,
        Material* material
    )
    {
        assert(lodLevel < mLODs.size() && material);

        auto it = std::find(mMaterials.begin(), mMaterials.end(), material);
        uint32_t materialIndex;
        if (it == mMaterials.end())
        {
            materialIndex = static_cast<uint32_t>(mMaterials.size());
            mMaterials.emplace_back(material);
        }
        else
        {
            materialIndex = static_cast<uint32_t>(it - mMaterials.begin());
        }

        LOD& lod = mLODs[lodLevel];

        assert(lod.vertexData.getSize() > 0);

        lod.submeshes.emplace_back(
            vertexOffset,
            vertexCount,
            indexOffset,
            indexCount,
            materialIndex
        );

        dirtyBound();
    }

    const Boxf& Mesh::getOrUpdateBoundingBox() noexcept
    {
        if (mBoundDirty)
            updateBound();
        return mBoundingBox;
    }

    GeometryProxyHandle Mesh::getOrCreateGeometryProxy(uint32_t lodLevel)
    {
        assert(lodLevel < mLODs.size());
        mLODGeometryProxies.resize(mLODs.size());
        auto& renderFrontend = Context::get().getRenderFrontend();
        if (!mLODGeometryProxies[lodLevel].isValid())
        {
            mLODGeometryProxies[lodLevel] = renderFrontend.allocateProxyHandle<ProxyType::Geometry>();
            createGeometryProxy(mLODGeometryProxies[lodLevel], lodLevel);
        }

        return mLODGeometryProxies[lodLevel];
    }

    void Mesh::postLoad()
    {
        // BoundingBox 已经作为普通属性从磁盘读回来了,不需要摸一遍顶点数据重新算——
        // mBoundDirty 本身不参与序列化,反序列化完默认还是构造时的 true,这里手动清掉
        mBoundDirty = false;
    }

    void Mesh::preSave()
    {
        // 如果这份 Mesh 从创建到现在从没被查询过包围盒(mBoundDirty 还是 true),
        // 存盘前必须强制刷新一次,否则 BoundingBox 属性存下去的会是默认构造的 Boxf{},
        // 而不是真实包围盒
        if (mBoundDirty)
            updateBound();
    }

    void Mesh::updateBound() noexcept
    {
        assert(mBoundDirty && mLODs.size() > 0);

        // 根据 LOD0 计算整个 Mesh 的包围体
        LOD& lod0 = mLODs[0];

        // 常态是:磁盘加载出来的 Mesh 顶点数据默认 Unloaded(上传 GPU 后也会被主动 unload 掉)——
        // 这里按需临时 load 一下,用完照原样还原,不改变调用前的常驻状态
        bool wasVertexDataLoaded = lod0.vertexData.isLoaded();
        if (!wasVertexDataLoaded)
            lod0.vertexData.load();

        uint32_t totalVertexCount = 0;
        for (auto& submesh : lod0.submeshes)
            totalVertexCount += submesh.vertexCount;

        size_t positionOffset = 0;
        for (auto& vertexAttribute : lod0.vertexAttributes)
        {
            if (vertexAttribute.type == VertexAttributeType::Position)
            {
                break;
            }
            else
            {
                positionOffset += vertexAttribute.stride * totalVertexCount;
            }
        }

        const float3* positions = reinterpret_cast<const float3*>(lod0.vertexData.getData().data() + positionOffset);
        float3 min{ std::numeric_limits<float>::max() };
        float3 max{ -std::numeric_limits<float>::max() };
        for (uint32_t i = 0; i < totalVertexCount; ++i)
        {
            min = glm::min(min, positions[i]);
            max = glm::max(max, positions[i]);
        }
        mBoundingBox = Boxf(min, max);

        if (!wasVertexDataLoaded)
            lod0.vertexData.unload();

        mBoundDirty = false;
    }

    rhi::IndexType indexTypeToRHIIndexType(IndexType type) noexcept
    {
        switch (type)
        {
        case IndexType::Uint16: return rhi::IndexType::Uint16;
        case IndexType::Uint32: return rhi::IndexType::Uint32;
        default: return rhi::IndexType::Uint16;
        };
    }

    void Mesh::createGeometryProxy(GeometryProxyHandle handle, uint32_t lodLevel)
    {
        LOD& lod = mLODs[lodLevel];
        uint64_t offset = 0;
        uint32_t totalVertexCount = 0;
        for (auto& submesh : lod.submeshes)
            totalVertexCount += submesh.vertexCount;
        VertexAttributeMask vertexAttributeMask = VertexAttributeMask::None;
        uint8_t texCoordOffset = 0;

        // vertexData 里各属性是按 lod.vertexAttributes 的原始(资产里录入的)顺序首尾拼接的,
        // 而 VertexLayout::writeVertexLayoutBindingsAndAttributes() 生成 pipeline 的 binding
        // 顺序是固定的优先级顺序(Position/Normal/Tangent/Color/TexCoord0..7), 和资产原始顺序
        // 未必一致——所以这里先按原始顺序算出每个属性的字节偏移, 记到按 VertexAttributeMask
        // 位对齐的表里, 再按那份固定优先级顺序重新排列输出, 而不是直接按原始顺序 push。
        // 不需要重排 lod.vertexAttributes 本身或搬动 vertexData 的字节, 两边只要用同一份优先级
        // 顺序表就能对齐
        static constexpr uint32_t kAttributeSlotCount = 12; // Position/Normal/Tangent/Color + TexCoord0..7
        std::array<uint64_t, kAttributeSlotCount> offsetBySlot{};
        for (auto& vertexAttribute : lod.vertexAttributes)
        {
            uint32_t slot = ~0u;
            switch (vertexAttribute.type)
            {
            case VertexAttributeType::Position:
                vertexAttributeMask |= VertexAttributeMask::Position;
                slot = 0;
                break;
            case VertexAttributeType::Normal:
                vertexAttributeMask |= VertexAttributeMask::Normal;
                slot = 1;
                break;
            case VertexAttributeType::Tangent:
                vertexAttributeMask |= VertexAttributeMask::Tangent;
                slot = 2;
                break;
            case VertexAttributeType::Color:
                vertexAttributeMask |= VertexAttributeMask::Color;
                slot = 3;
                break;
            case VertexAttributeType::TexCoord:
                if (texCoordOffset < 8)
                {
                    vertexAttributeMask |= VertexAttributeMask(static_cast<uint32_t>(VertexAttributeMask::TexCoord0) << texCoordOffset);
                    slot = 4 + texCoordOffset;
                    ++texCoordOffset;
                }
                break;
            default:
                break;
            }

            if (slot != ~0u)
            {
                offsetBySlot[slot] = offset;
            }
            offset += vertexAttribute.stride * totalVertexCount;
        }

        std::vector<uint64_t> vertexBindingOffsets;
        vertexBindingOffsets.reserve(kAttributeSlotCount);
        for (uint32_t slot = 0; slot < kAttributeSlotCount; ++slot)
        {
            if (enumHasAnyFlags(vertexAttributeMask, VertexAttributeMask(1u << slot)))
            {
                vertexBindingOffsets.emplace_back(offsetBySlot[slot]);
            }
        }
        std::vector<GeometryProxy::Section> sections;
        for (auto& submesh : lod.submeshes)
        {
            GeometryProxy::Section section;
            section.vertexOffset = submesh.vertexOffset;
            section.firstIndex = submesh.indexOffset;
            section.indexCount = submesh.indexCount;
            sections.emplace_back(section);
        }

        // 上传 GPU 只需要这一份拷贝,拷完就不再需要 CPU 侧常驻——主动 unload 掉,
        // 不用等渲染线程真正消费完这份拷贝(它已经是独立的一份数据了)
        lod.vertexData.load();
        lod.indexData.load();
        std::vector<uint8_t> vertexData(lod.vertexData.getData().begin(), lod.vertexData.getData().end());
        std::vector<uint8_t> indexData(lod.indexData.getData().begin(), lod.indexData.getData().end());
        lod.vertexData.unload();
        lod.indexData.unload();

        Context::get().getRenderFrontend().enqueueLambda(
            [this,
            handle,
            vertexAttributeMask,
            vertexData = std::move(vertexData),
            vertexBindingOffsets = std::move(vertexBindingOffsets),
            indexData = std::move(indexData),
            indexType = indexTypeToRHIIndexType(lod.indexType),
            sections = std::move(sections)](FrameContext& context) mutable
            {
                context.getProxyManager().createProxy(
                    handle,
                    VertexLayout(VertexLayoutType::Static_Mesh, vertexAttributeMask),
                    std::move(vertexData),
                    std::move(vertexBindingOffsets),
                    std::move(indexData),
                    indexType,
                    std::move(sections)
                );
            }
        );
    }

    void Mesh::destroyGeometryProxy(GeometryProxyHandle handle) noexcept
    {
        assert(handle.isValid());
        Context::get().getRenderFrontend().enqueueLambda(
            [handle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

    template <> Type* Reflection::createType<Mesh::Submesh>()
    {
        auto s = new TStructure<Mesh::Submesh>("Mesh.Submesh");
        s->addProperty("VertexOffset", &Mesh::Submesh::vertexOffset);
        s->addProperty("VertexCount", &Mesh::Submesh::vertexCount);
        s->addProperty("IndexOffset", &Mesh::Submesh::indexOffset);
        s->addProperty("IndexCount", &Mesh::Submesh::indexCount);
        s->addProperty("MaterialIndex", &Mesh::Submesh::materialIndex);
        return s;
    }

    template <> Type* Reflection::createType<Mesh::LOD>()
    {
        auto s = new TStructure<Mesh::LOD>("Mesh.LOD");
        s->addProperty("VertexData", &Mesh::LOD::vertexData);
        s->addProperty("VertexAttributes", &Mesh::LOD::vertexAttributes);
        s->addProperty("IndexData", &Mesh::LOD::indexData);
        s->addProperty("IndexType", &Mesh::LOD::indexType);
        s->addProperty("Submeshes", &Mesh::LOD::submeshes);
        s->addProperty("MinScreenPercentage", &Mesh::LOD::minScreenPercentage);
        return s;
    }

    template <> Type* Reflection::createType<Mesh>()
    {
        auto c = new TClass<Mesh>("Mesh");
        c->addProperty("LODs", &Mesh::mLODs);
        c->addProperty("Materials", &Mesh::mMaterials);
        c->addProperty("BoundingBox", &Mesh::mBoundingBox);
        return c;
    }
}
