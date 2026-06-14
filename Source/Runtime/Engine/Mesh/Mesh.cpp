#include "Mesh.h"

#include <RenderBase/RenderFrontend.h>
#include <RenderBase/GeometryProxy.h>
#include "../Context.h"

namespace hyp
{
    REGISTER_CLASS(Mesh)

    Mesh::Mesh()
    {

    }

    Mesh::~Mesh() noexcept
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        for (auto& geometryProxy : mLODGeometryProxies)
        {
            if (geometryProxy.isValid())
            {
                destroyGeometryProxy(geometryProxy);
                renderFrontend.freeProxyHandle(geometryProxy);
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
        lod.vertexData = std::move(vertexData);
        lod.vertexAttributes = std::move(vertexAttributes);
        lod.indexData = std::move(indexData);
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

        assert(!lod.vertexData.empty());

        lod.submeshes.emplace_back(
            vertexOffset,
            vertexCount,
            indexOffset,
            indexCount,
            materialIndex
        );

        dirtyBound();
    }

    const Boxf& Mesh::getBoundingBox() const noexcept
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

    void Mesh::updateBound() const noexcept
    {
        assert(mBoundDirty && mLODs.size() > 0);

        // 根据 LOD0 计算整个 Mesh 的包围体
        const LOD& lod0 = mLODs[0];

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

        const float3* positions = reinterpret_cast<const float3*>(lod0.vertexData.data() + positionOffset);
        float3 min{ std::numeric_limits<float>::max() };
        float3 max{ -std::numeric_limits<float>::max() };
        for (uint32_t i = 0; i < totalVertexCount; ++i)
        {
            min = glm::min(min, positions[i]);
            max = glm::max(max, positions[i]);
        }
        mBoundingBox = Boxf(min, max);

        mBoundDirty = false;
    }

    VkIndexType indexTypeToVkIndexType(Mesh::IndexType type) noexcept
    {
        switch (type)
        {
        case Mesh::IndexType::Uint16: return VK_INDEX_TYPE_UINT16;
        case Mesh::IndexType::Uint32: return VK_INDEX_TYPE_UINT32;
        default: return VK_INDEX_TYPE_UINT16;
        };
    }

    void Mesh::createGeometryProxy(GeometryProxyHandle handle, uint32_t lodLevel)
    {
        LOD& lod = mLODs[lodLevel];
        std::vector<VkDeviceSize> vertexBindingOffsets;
        VkDeviceSize offset = 0;
        uint32_t totalVertexCount = 0;
        for (auto& submesh : lod.submeshes)
            totalVertexCount += submesh.vertexCount;
        VertexAttributeMask vertexAttributeMask = VertexAttributeMask::None;
        uint8_t texCoordOffset = 0;
        for (auto& vertexAttribute : lod.vertexAttributes)
        {
            vertexBindingOffsets.emplace_back(offset);
            offset += vertexAttribute.stride * totalVertexCount;
            switch (vertexAttribute.type)
            {
            case VertexAttributeType::Position: 
                vertexAttributeMask |= VertexAttributeMask::Position;
                break;
            case VertexAttributeType::Normal:
                vertexAttributeMask |= VertexAttributeMask::Normal;
                break;
            case VertexAttributeType::Tangent:
                vertexAttributeMask |= VertexAttributeMask::Tangent;
                break;
            case VertexAttributeType::Color:
                vertexAttributeMask |= VertexAttributeMask::Color;
                break;
            case VertexAttributeType::TexCoord:
                if (texCoordOffset < 8)
                {
                    vertexAttributeMask |= VertexAttributeMask(static_cast<uint32_t>(VertexAttributeMask::TexCoord0) << texCoordOffset++);
                }
                break;
            default:
                break;
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

        Context::get().getRenderFrontend().enqueueLambda(
            [this,
            handle,
            vertexAttributeMask,
            vertexData = lod.vertexData,
            vertexBindingOffsets = std::move(vertexBindingOffsets),
            indexData = lod.indexData,
            indexType = indexTypeToVkIndexType(lod.indexType),
            sections = std::move(sections)](RenderFrameContext& context) mutable
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
            [handle](RenderFrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

    template <> Type* Reflection::createType<Mesh>()
    {
        auto c = new TClass<Mesh>("Mesh");
        return c;
    }
}
